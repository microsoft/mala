// NetBursts.cpp : Defines the entry point for the console application.
//

#pragma once

#include "stdafx.h"
#include "Scheduler.hpp"
#include "MiscWin.hpp"

#include <memory>
#include <stack>
#include <vector>

using namespace std;

namespace Schedulers
{
    static const size_t ALIGNMENT = 16;

	ActionArena::ActionArena(ArenaSet& set, void* bytes, uint32_t sizeLimit)
		: m_sizeLimit(sizeLimit)
		, m_disposableOffset(sizeLimit)
		, m_pSet(&set)
		, m_arenaBytes(bytes)
		, m_offset(0)
	{}

    void ActionArena::Retire()
    {
		Audit::Assert(0 != m_offset,
            L"retiring the same action twice?");
        m_offset = 0;

		// Call all registered Dispose fuctions.
		auto pDisposable = (mbuf::Disposable**)(m_disposableOffset + (char*)m_arenaBytes);
		auto pDisposableEnd = (mbuf::Disposable**)(m_sizeLimit + (char*)m_arenaBytes);

		while (pDisposable < pDisposableEnd)
		{
			Audit::Assert(nullptr != *pDisposable, L"Disposable object is null!");
			(*pDisposable)->Dispose();
			pDisposable++;
		}

		m_disposableOffset = m_sizeLimit;

        // This code is on the critical path, we use zero memory
        // to ensure safety, however it may have performance impact
        // TODO!! performance experiment to decide whether to keep this.
        // ZeroMemory(m_arenaBytes, m_sizeLimit);
        m_pSet->Push(this);
    }

    class ThreadSafeArenaSet : public ArenaSet
    {
		vector<ActionArena*> m_allArenas;
		const size_t m_unitSize;

        std::unique_ptr<mbuf::LargePageBuffer> m_pBuffer;
        mbuf::CircularQue<ActionArena*> m_arenas;

    public:
        ThreadSafeArenaSet(size_t unitSize, uint32_t count)
            : m_unitSize(unitSize), m_arenas(count)
        {
            Audit::Assert(((unitSize - 1) ^ (0 - unitSize)) == ~0, 
                L"unitSize must be a power of 2");
            Audit::Assert(unitSize <= (1ull << 26), 
                L"unitSize must be less than or equal to 64MB");
            Audit::Assert(0 < count, L"non zero count expected");

            Audit::Assert((unitSize * count) < (1ull << 30), L"limit total of arenas to 1GB");

            size_t actualSize;
            unsigned actualCount;
            m_pBuffer = std::make_unique<mbuf::LargePageBuffer>(unitSize, count, actualSize, actualCount);
            for (unsigned i = 0; i < actualCount; ++i)
            {
				auto pArena = new ActionArena{ *this, &m_pBuffer->GetData()[i * unitSize], (uint32_t) unitSize };
				m_allArenas.push_back(pArena);
                m_arenas.Add(pArena);
            }
        }

        ActionArena* Pop()
        {
            ActionArena* pArena = nullptr;
            auto res = m_arenas.Get(pArena);
            Audit::Assert(res, L"Arena pool empty!");
			Audit::Assert(pArena->IsEmpty(),
                L"popped an arena which is still in use");
            return pArena;
        }

        void Push(ActionArena* pArena)
        {
            Audit::Assert(pArena->IsEmpty(), 
                L"pushing an arena which is still in use");
            m_arenas.Add(pArena);                        
        }

		ActionArena* GetActionArena(void* pData)
		{
			size_t arenaIndex = (size_t) m_pBuffer->OffsetOf(pData) / m_unitSize;
			Audit::Assert(arenaIndex < m_allArenas.size(), 
                L"the argument did not point inside the ArenaSet");
			return m_allArenas[arenaIndex];
		}

        ~ThreadSafeArenaSet()
		{
			for (auto pArena : m_allArenas)
				delete pArena;
		}
    };

    unique_ptr<ArenaSet> ArenaSet::ArenaSetFactory(size_t unitSize, uint32_t count)
    {
        return make_unique<ThreadSafeArenaSet>(unitSize, count);
    }

    ActionArena* ContinuationBase::GetArena() const
    {
        return m_activity.GetScheduler()->GetArena((void*)this);
    }

}