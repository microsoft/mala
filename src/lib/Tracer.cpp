#include "stdafx.h"

#include "Audit.hpp"
#include "MiscWin.hpp"
#include "ETWTracing.h"

#include "Tracer.hpp"

#include "Scheduler.hpp"


#include <objbase.h>
#include <random>
#include <limits>
#include <unordered_map>
#include <sstream>
#include <memory>


namespace  // private to this source file
{

	class NoneZeroRandom32 {
	private:
		std::random_device rd;
		std::mt19937_64 rng;
		std::tr1::uniform_int<uint32_t> unif;
	public:
		NoneZeroRandom32() : rng(rd()), unif(1, 0xffffffff)
		{
		}

		uint32_t Get() {
			return unif(rng);
		}
	};

	NoneZeroRandom32 g_noneZeroRandom;


	// Data structure for enabling ETW tracing
	//
#define DEFAULT_LOG_PATH L".\\Tracing.etl"
#define SESSION_NAME L"SessionTracing"

	struct ETWControl {
		// {314B2974-635B-46A1-9E3E-64751E038EFF}
		EVENT_TRACE_PROPERTIES* m_pSessionProperties = nullptr;
		uint64_t m_sessionHandle = 0;
		bool m_traceOn = false;
	}  g_etwControl;

	const GUID SessionGuid = { 0x314B2974, 0x635B, 0x46A1, { 0x9E, 0x3E, 0x64, 0x75, 0x1E, 0x03, 0x8E, 0xFF } };
}

namespace Schedulers
{
	extern __declspec(thread) const Schedulers::Work* threadActiveJob;
}
namespace Tracer
{
	using namespace std;

	const TraceRec* GetCurrentTraceHeader()
	{
		if (Schedulers::threadActiveJob == nullptr)
			return nullptr;
		return Schedulers::threadActiveJob->GetTracer();
	}

	TraceId::TraceId(bool mustLog)
	{
		Audit::Assert(S_OK == CoCreateGuid(reinterpret_cast<GUID*>(&m_id)), L"Failed to create new GUID");
		if (mustLog)
		{
			uint32_t* value = reinterpret_cast<uint32_t*>(&m_id);
			value[3] &= ~TRACE_SAMPLE_MASK;
		}
	}

	TraceRec::TraceRec(const wchar_t* name, bool mustlog)
		: m_traceId(mustlog)
		, m_spanId(g_noneZeroRandom.Get())
		, m_parentSpanId(0)
	{
		if (m_traceId.ShouldSample())
		{
			EventWriteSpanStart(reinterpret_cast<const GUID*>(&(m_traceId)), m_parentSpanId, m_spanId,
				static_cast<uint32_t>(Counter::NonCounter), 0, name);
		}
	}

	TraceRec::TraceRec(const wchar_t* name, const TraceRec& parent)
		: m_traceId(parent.m_traceId)
		, m_parentSpanId(parent.m_spanId)
		, m_spanId(g_noneZeroRandom.Get())
	{
		if (m_traceId.ShouldSample())
		{
			EventWriteSpanStart(reinterpret_cast<const GUID*>(&(m_traceId)), m_parentSpanId, m_spanId,
				static_cast<uint32_t>(Counter::NonCounter), 0, name);
		}
	}

	TraceRec::TraceRec(uint64_t low, uint64_t high, uint32_t parentSpan, uint32_t spanId)
		: m_traceId(low, high)
		, m_parentSpanId(parentSpan)
		, m_spanId(spanId == 0 ? g_noneZeroRandom.Get() : spanId)
	{
	}

	bool TraceRec::ShouldSample() const
	{
		return  m_traceId.ShouldSample();
	}

	void InitializeLogging(const wchar_t * logFilePath)
	{
		if (logFilePath == nullptr)
		{
			logFilePath = DEFAULT_LOG_PATH;
		}

		auto status = S_OK;
		uint32_t BufferSize = 0;

		// Allocate memory for the session properties. The memory must
		// be large enough to include the log file name and session name,
		// which get appended to the end of the session properties structure.

		uint32_t logPathSize = (uint32_t)wcslen(logFilePath);
		Audit::Assert(logPathSize > 0, L"Invalide log file path!");
		logPathSize = sizeof(wchar_t)*(logPathSize + 1);

		BufferSize = sizeof(EVENT_TRACE_PROPERTIES) 
            + logPathSize + sizeof(SESSION_NAME);
		g_etwControl.m_pSessionProperties = 
            (EVENT_TRACE_PROPERTIES*)malloc(BufferSize);

		Audit::Assert(nullptr != g_etwControl.m_pSessionProperties, 
            L"Unable to allocate memory for ETW tracing session!");

		// Set the session properties. You only append the log file name
		// to the properties structure; the StartTrace function appends
		// the session name for you.

		ZeroMemory(g_etwControl.m_pSessionProperties, BufferSize);
		g_etwControl.m_pSessionProperties->Wnode.BufferSize = BufferSize;
		g_etwControl.m_pSessionProperties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
		g_etwControl.m_pSessionProperties->Wnode.ClientContext = 1; //QPC clock resolution
		g_etwControl.m_pSessionProperties->Wnode.Guid = SessionGuid;
		g_etwControl.m_pSessionProperties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
		g_etwControl.m_pSessionProperties->MaximumFileSize = 100;  // 100 MB
		g_etwControl.m_pSessionProperties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
		g_etwControl.m_pSessionProperties->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES)+sizeof(SESSION_NAME);
		memcpy((void *)((char*)g_etwControl.m_pSessionProperties + g_etwControl.m_pSessionProperties->LogFileNameOffset), logFilePath, logPathSize);

		// Create the trace session.

		status = StartTrace((PTRACEHANDLE)&g_etwControl.m_sessionHandle, SESSION_NAME, g_etwControl.m_pSessionProperties);
		if (S_OK != status && ERROR_ALREADY_EXISTS != status)
		{
			DisposeLogging();
			Audit::OutOfLine::Fail((StatusCode)status, L"Failed to start ETW Tracing!");
		}

		// Enable the providers that you want to log events to your session.

		if (status == S_OK)
		{
			status = EnableTraceEx2(
				g_etwControl.m_sessionHandle,
				&MicrosoftWampum,
				EVENT_CONTROL_CODE_ENABLE_PROVIDER,
				TRACE_LEVEL_INFORMATION,
				0,
				0,
				0,
				nullptr
				);
			if (S_OK != status)
			{
				DisposeLogging();
				Audit::OutOfLine::Fail((StatusCode)status, 
                    L"Failed to enable ETW tracing!");
			}
		}

		status = EventRegisterMicrosoft_Wampum();
		if (S_OK != status)
		{
			DisposeLogging();
			Audit::OutOfLine::Fail((StatusCode)status,
                L"Failed to register as ETW provider!");
		}
		g_etwControl.m_traceOn = true;
	}

	void DisposeLogging()
	{
		{
			EventUnregisterMicrosoft_Wampum(); // ignore errors
			g_etwControl.m_traceOn = false;
		}

		{
		EnableTraceEx2(
			g_etwControl.m_sessionHandle,
			&MicrosoftWampum,
			EVENT_CONTROL_CODE_DISABLE_PROVIDER,
			TRACE_LEVEL_INFORMATION,
			0,
			0,
			0,
			nullptr
			);

		ControlTrace(g_etwControl.m_sessionHandle, SESSION_NAME, 
            g_etwControl.m_pSessionProperties, EVENT_TRACE_CONTROL_STOP);
		g_etwControl.m_sessionHandle = 0;
	}

		//        if (g_etwControl.m_pSessionProperties)
		{
			free(g_etwControl.m_pSessionProperties);
			g_etwControl.m_pSessionProperties = nullptr;
		}
	}


	const TraceRec* ChooseDefaultIfNULL(const TraceRec* span)
	{
		if (span == nullptr)
		{
			span = GetCurrentTraceHeader();
		}
		return span;
	}

	const TraceRec* ChooseDefaultEmptyIfNULL(const TraceRec* span)
	{
		span = ChooseDefaultIfNULL(span);
		if (span == nullptr)
		{
			span = &g_emptySpan;
		}
		return span;
	}

	void EndSpan(const TraceRec* span)
	{
		span = ChooseDefaultIfNULL(span);
		Audit::Assert(span != nullptr, L"Can not locate tracing header!");
		if (span->ShouldSample())
		{
			EventWriteSpanEnd(reinterpret_cast<const GUID*>(&(span->m_traceId)), span->m_parentSpanId, span->m_spanId,
				static_cast<uint32_t>(Counter::NonCounter), 0, L"");
		}
	}

	void RPCClientSend(const TraceRec* span)
	{
		span = ChooseDefaultIfNULL(span);
		Audit::Assert(span != nullptr, L"Can not locate tracing header!");
		if (span->ShouldSample())
		{
			EventWriteClientSend(reinterpret_cast<const GUID*>(&(span->m_traceId)), span->m_parentSpanId, span->m_spanId,
				static_cast<uint32_t>(Counter::NonCounter), 0, L"");
		}
	}

	void RPCClientReceive(const TraceRec* span)
	{
		span = ChooseDefaultIfNULL(span);
		Audit::Assert(span != nullptr,
            L"Can not locate tracing header!");
		if (span->ShouldSample())
		{
			EventWriteClientReceive(reinterpret_cast<const GUID*>(&(span->m_traceId)), span->m_parentSpanId, span->m_spanId,
				static_cast<uint32_t>(Counter::NonCounter), 0, L"");
		}
	}

	void RPCClientTimeout(const TraceRec* span)
	{
		span = ChooseDefaultIfNULL(span);
		Audit::Assert(span != nullptr, 
            L"Can not locate tracing header!");
		if (span->ShouldSample())
			EventWriteClientTimeout(reinterpret_cast<const GUID*>(&(span->m_traceId)), span->m_parentSpanId, span->m_spanId,
			static_cast<uint32_t>(Counter::NonCounter), 0, L"");
	}

	void RPCServerReceive(const TraceRec* span)
	{
		span = ChooseDefaultIfNULL(span);
		Audit::Assert(span != nullptr, 
            L"Can not locate tracing header!");
		if (span->ShouldSample())
		{
			EventWriteServerReceive(reinterpret_cast<const GUID*>(&(span->m_traceId)), span->m_parentSpanId, span->m_spanId,
				static_cast<uint32_t>(Counter::NonCounter), 0, L"");
		}
	}

	void RPCServerSend(const TraceRec* span)
	{
		span = ChooseDefaultIfNULL(span);
		Audit::Assert(span != nullptr, 
            L"Can not locate tracing header!");
		if (span->ShouldSample())
			EventWriteServerSend(reinterpret_cast<const GUID*>(&(span->m_traceId)), span->m_parentSpanId, span->m_spanId,
			static_cast<uint32_t>(Counter::NonCounter), 0, L"");
	}

	void LogError(StatusCode error, const wchar_t * msg, const TraceRec* span)
	{
		span = ChooseDefaultEmptyIfNULL(span);
		if (!span->ShouldSample())
		{
			return;
		}
		wchar_t* detail = (msg == nullptr) ? L"" : msg;
		EventWriteSpanException(reinterpret_cast<const GUID*>(&(span->m_traceId)), span->m_parentSpanId, span->m_spanId,
			static_cast<uint32_t>(Counter::NonCounter), (int)error, detail);
	}

	void LogWarning(StatusCode error, const wchar_t * msg, const TraceRec* span)
	{
		span = ChooseDefaultEmptyIfNULL(span);
		if (!span->ShouldSample())
		{
			return;
		}
		EventWriteSpanWarning(reinterpret_cast<const GUID*>(&(span->m_traceId)), span->m_parentSpanId, span->m_spanId,
			static_cast<uint32_t>(Counter::NonCounter), (int)error, msg);
	}

	void LogInfo(StatusCode error, const wchar_t * msg, const TraceRec* span)
	{
		span = ChooseDefaultEmptyIfNULL(span);
		if (!span->ShouldSample())
		{
			return;
		}
		EventWriteSpanInfo(reinterpret_cast<const GUID*>(&(span->m_traceId)), span->m_parentSpanId, span->m_spanId,
			static_cast<uint32_t>(Counter::NonCounter), (int)error, msg);
	}

	void LogDebug(StatusCode error, const wchar_t * msg, const TraceRec* span)
	{
		span = ChooseDefaultEmptyIfNULL(span);
		if (!span->ShouldSample())
		{
			return;
		}
		EventWriteSpanDebug(reinterpret_cast<const GUID*>(&(span->m_traceId)), span->m_parentSpanId, span->m_spanId,
			static_cast<uint32_t>(Counter::NonCounter), (int)error, msg);
	}

	void LogCounterValue(Counter counter, const wchar_t* counterInstance, uint64_t value, const TraceRec* span)
	{
		span = ChooseDefaultEmptyIfNULL(span);
		if (span->ShouldSample())
		{
			EventWriteCounterValue(
				reinterpret_cast<const GUID*>(&(span->m_traceId)),
				span->m_parentSpanId, span->m_spanId,
				static_cast<uint32_t>(counter),
				value, counterInstance);
		}
	}
}
