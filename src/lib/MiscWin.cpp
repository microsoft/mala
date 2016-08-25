// NetBursts.cpp : Defines the entry point for the console application.
//

#pragma once

#include "stdafx.h"

#include <utility>
#include <algorithm>
#include <vector>
#include <stack>

#include "Audit.hpp"
#include "Mem.hpp"
#include "MiscWin.hpp"


using namespace std;

const StatusCode H_ok = S_OK;
const StatusCode H_unexpected = E_UNEXPECTED;
const StatusCode H_notimplemented = E_NOTIMPL;

namespace Audit
{
    // Define type of status code or error code, a lame effort in hiding
    // windows specifics from header files.

    void OutOfLine::Fail(const wchar_t* utf8message, ...)
    {
        auto status = (StatusCode)::GetLastError();
        va_list arg;
        va_start(arg, utf8message);
        OutOfLine::vFail(status, utf8message, arg);
        va_end(arg);
    }

}


namespace mbuf
{
    void EnableLargePages()
    {
        HANDLE      hToken;
        TOKEN_PRIVILEGES tp;

        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        {
            Audit::OutOfLine::Fail((StatusCode)GetLastError());
        }
        // the app needs to run at elevated privilege, with local security policy allowing memory locking.
        auto closer = Ctrl::AutoClose(hToken,
            [&] {
            if (!LookupPrivilegeValue(nullptr, TEXT("SeLockMemoryPrivilege"), &tp.Privileges[0].Luid))
            {
                Audit::OutOfLine::Fail((StatusCode)::GetLastError());
            }
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, 0);
            // AdjustToken silently fails, so we always need to check last status.
            auto status = (StatusCode)::GetLastError();
            if (status != H_ok)
            {
                if (status == (StatusCode)ERROR_PRIVILEGE_NOT_HELD)
                    Audit::OutOfLine::Fail(L"run at elevated privilege, with local security policy allowing memory locking");
                else
                    Audit::OutOfLine::Fail((StatusCode)status);
            }
        });
    }

    std::once_flag g_largePageEnabled;

    /* Allocate multiple units of buffers contiguously in large pages which will stay resident
    */
    LargePageBuffer::LargePageBuffer(
        const size_t oneBufferSize,
        const unsigned bufferCount,
        __out size_t& allocatedSize,
        __out unsigned& allocatedBufferCount
    )
    {
        std::call_once(g_largePageEnabled, EnableLargePages);

        m_pBuffer = nullptr;
        m_allocatedSize = 0;
        const size_t granularity = ::GetLargePageMinimum();
        const size_t minimumSize = oneBufferSize * bufferCount;
        allocatedSize = (minimumSize + granularity - 1) & (0 - granularity);
        allocatedBufferCount = 0;

        m_pBuffer = ::VirtualAlloc(
            nullptr,
            allocatedSize,
            MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES,
            PAGE_READWRITE
        );

        if (m_pBuffer != nullptr)
        {
            m_allocatedSize = allocatedSize;
            allocatedBufferCount = (int)(allocatedSize / oneBufferSize);
        }
        else
        {
            Audit::OutOfLine::Fail((StatusCode)::GetLastError(), L"Cannot allocate large page!");
        }
    }

    LargePageBuffer::~LargePageBuffer()
    {
        if (m_pBuffer != nullptr)
        {
            ::VirtualFreeEx(GetCurrentProcess(), m_pBuffer, m_allocatedSize, 0);
            m_pBuffer = nullptr;
        }
    }
}