#pragma once

#include "stdafx.h"

#include <cstdarg>

using StatusCode = long; // HRESULT
extern const StatusCode H_ok;
extern const StatusCode H_unexpected;
extern const StatusCode H_notimplemented;

namespace Audit
{

    /* Assertions designed for use in production code (not limited to debug builds).
    Avoid the evaluation of arguments except in failure paths.  The only
    cost for normal paths should be the overhead of the predicate and
    a predictable branch.
    */
    struct OutOfLine
    {
        static void Fail(const wchar_t* utf8message, ...);

        static void Fail(const StatusCode status);

        static void Fail(const StatusCode status, const wchar_t* const message, ...);

        static void vFail(const StatusCode status, const wchar_t* const message, va_list args);
    };

    // evaluate conditions inline to avoid evaluating arguments until necessary

    inline void Assert(StatusCode status)
    {
        if (status != H_ok)
        {
            OutOfLine::Fail(status);
        }
    }

    inline void Assert(bool invariant)
    {
        if (!invariant)
        {
            OutOfLine::Fail(L"assert");
        }
    }

    inline void Assert(bool invariant, const wchar_t* utf8message, ...)
    {
        if (!invariant)
        {
            va_list arg;
            va_start(arg, utf8message);
            OutOfLine::vFail(H_unexpected, utf8message, arg);
            va_end(arg);
        }
    }

    inline void ArgNotNull(void* value, const wchar_t* utf8message)
    {
        if (value == nullptr)
        {
            OutOfLine::Fail(H_unexpected, utf8message);
        }
    }

    inline void NotImplemented()
    {
        OutOfLine::Fail(H_notimplemented, L"Not Implemented");
    }

    inline void NotImplemented(const wchar_t* utf8message)
    {
        OutOfLine::Fail(H_notimplemented, utf8message);
    }
}

