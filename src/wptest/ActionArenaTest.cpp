#pragma once
#include "stdafx.h"

#include <iostream>
#include <vector>

#include "Audit.hpp"
#include "Control.hpp"
#include "Mem.hpp"
#include "Scheduler.hpp"

#include "CppUnitTest.h"

namespace EBTest
{
	using namespace std;
	using namespace Microsoft::VisualStudio::CppUnitTestFramework;
    using namespace Schedulers;

	class DisposableTestBuffer : public mbuf::Disposable
	{
	private:
		bool& m_disposed;
        char m_padding[8];
	public:
		DisposableTestBuffer(bool& disposed) : m_disposed(disposed)
		{
		}

		void Dispose() override
		{
			m_disposed = true;
		}
	};

	TEST_CLASS(ActionArenaTest)
	{
		const uint32_t m_expectedNumberOfObjects = 1024;
	public:
		TEST_METHOD(ArenaRegisterDisposable)
		{

			// caculate the size of arena needed
			auto pDisposableTestBufferSz = static_cast<uint32_t>(sizeof(DisposableTestBuffer));
			uint32_t allocatedTestBufferSz = (pDisposableTestBufferSz + 7) & ~7;
			auto pDisposableSz = static_cast<uint32_t>(sizeof(mbuf::Disposable*));
			auto arenaSize = m_expectedNumberOfObjects*(allocatedTestBufferSz + pDisposableSz);

			unique_ptr<ArenaSet> pArenaSet = ArenaSet::ArenaSetFactory(arenaSize, 128);
			ActionArena* actionArena = pArenaSet->Pop();
			uint32_t remainingSz = arenaSize;
			Audit::Assert(remainingSz == actionArena->RemainingSpace(), 
                L"Allocate ActionArena failed.");

			// boolActionArena is used to allocate bool object.
			ActionArena* boolArena = pArenaSet->Pop();
			vector<bool*> disposeObjVec;

			// keep allocating until the arena is full 
			while (actionArena->RemainingSpace() >= allocatedTestBufferSz + pDisposableSz)
			{
				auto pDisposeObj = boolArena->allocate<bool>(false);

				auto disposableTestBuffer = actionArena->allocate<DisposableTestBuffer>(*pDisposeObj);
				remainingSz -= allocatedTestBufferSz;
				Audit::Assert(remainingSz == actionArena->RemainingSpace(),
                    L"Remaining space is not correct");

				actionArena->RegisterDisposable(*disposableTestBuffer);
				remainingSz -= pDisposableSz;
				Audit::Assert(remainingSz == actionArena->RemainingSpace(), 
                    L"Remaining space is not correct");

				disposeObjVec.push_back(pDisposeObj);
			}

			Audit::Assert(m_expectedNumberOfObjects == disposeObjVec.size(),
                L"Dispose Object number should be the same as m_expectedNumberOfObjects.");
			Audit::Assert(actionArena->RemainingSpace() == 0,
                L"actionArena should be full.");

			for (auto iter = disposeObjVec.begin(); iter != disposeObjVec.end(); iter++)
			{
				Audit::Assert(!(**iter),
                    L"disposeObjs should not be disposed before actionArena retire.");
			}

			actionArena->Retire();

			for (auto iter = disposeObjVec.begin(); iter != disposeObjVec.end(); iter++)
			{
				Audit::Assert(**iter,
                    L"disposeObjs should be disposed before actionArena retire.");
			}

			boolArena->Retire();
		}
	};
}