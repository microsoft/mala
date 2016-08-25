// Non-portable utilities.

/*  This should only be #included into a .cpp file so as to avoid making
    other header files non-portable.
    (and because getting windows.h and other headers to play nice is tricky)
*/

#pragma once

#include "Control.hpp"
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif

#define NOMINMAX 

#include <windows.h>
#include <WinSock2.h>

#include <string>

namespace Ctrl
{

    template<typename Action>
    class HandleAutoCloser
    {
        HANDLE m_handle;
    public:
        HandleAutoCloser(_In_ HANDLE handle, _In_ Action act) {
            m_handle = handle;
            act();
        }
        ~HandleAutoCloser() { CloseHandle(m_handle); };
    };

    template<typename Action>
    HandleAutoCloser<Action> AutoClose(_In_ HANDLE h, _In_ Action act)
    {
        return HandleAutoCloser<Action>(h, act);
    }

    template<>
    struct SharedRegion < SRWLOCK >
    {
        static void Share(_In_ SRWLOCK& lock)
        {
            ::AcquireSRWLockShared(&lock);
        }
        static void Release(_In_ SRWLOCK& lock)
        {
            ::ReleaseSRWLockShared(&lock);
        }
    };

    template<>
    struct ExcludedRegion < SRWLOCK >
    {
        static void Exclude(_In_ SRWLOCK& lock)
        {
            ::AcquireSRWLockExclusive(&lock);
        }
        static void Release(_In_ SRWLOCK& lock)
        {
            ::ReleaseSRWLockExclusive(&lock);
        }
    };

    inline int64_t GetPerfTick()
    {
        LARGE_INTEGER ticksNow;
        Audit::Assert(0 != QueryPerformanceCounter(&ticksNow),
            L"QueryPerformanceCounter failed");
        return ticksNow.QuadPart;
    }

}
