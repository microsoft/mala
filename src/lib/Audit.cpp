// Procrastinate : an initial storage loop
//
#pragma once

#include "stdafx.h"

#include "Audit.hpp"
#include "Mem.hpp"
#include "Tracer.hpp"

#include <memory>
#include <unordered_map>
#include <utility>
#include <algorithm>

#include <cstdio>

using namespace std;

namespace Audit
{

    void OutOfLine::vFail(const StatusCode status, const wchar_t* const message, va_list args) 
    {
        static const size_t BufferLen = SI::Ki;
        wchar_t buffer[BufferLen];
        ::memset(buffer, 0, sizeof(buffer));

        int index = swprintf_s(buffer, L"0x%x: ", status);
        
        vswprintf_s(buffer+index, BufferLen - index, message, args);

        // write to stderr for easier experimenting 
        fwprintf_s(stderr, buffer);

        Tracer::LogError(status, buffer);

        std::terminate();
        //throw std::runtime_error{ buffer };
    }

    
    void OutOfLine::Fail(const StatusCode status, const wchar_t* const message, ...)
    {
        va_list arg;

        va_start(arg, message);
        OutOfLine::vFail(status, message, arg);
        va_end(arg);
    }

    void OutOfLine::Fail(const StatusCode status)
    {
        Fail(status, L"");
    }
}
