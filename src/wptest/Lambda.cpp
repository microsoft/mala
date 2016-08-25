#pragma once
#include "stdafx.h"

#include "SchedulerIOCP.hpp"
#include "FileManager.hpp"

#include "CppUnitTest.h"

#include "Windows.h"

#include <iostream>
#include <mutex>
#include <condition_variable>
#include <random>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Schedulers;

namespace FileQueueTest
{
    uint64_t gExpected[512];
    uint64_t gReadValue[512];
    std::mutex glock;
    std::condition_variable gReadyExit;

    TEST_CLASS(Lambda)
    {
    public:

        TEST_METHOD(IOCompletionLambda)
        {
            // Logging and thread pool setup
            Tracer::InitializeLogging(L"testlog.etl");

            auto hiocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
                nullptr, ULONG_PTR(0), 0);
            if (hiocp == nullptr) {
                Audit::OutOfLine::Fail(L"Could not create IOCP.");
            }

            auto pScheduler = SchedulerIOCPFactory(IOCPhandle(hiocp), 8, nullptr);
            Schedulers::Activity* pSeqJob = Schedulers::ActivityFactory(*pScheduler, L"File IO Span 1 ");

            // setup file
            auto fileQueue = AsyncFileIo::UnbufferedFileFactory(L"testfile.bin", *pScheduler, 4 * SI::Gi);

            gExpected[0] = 98765432101;
            fileQueue->Write(0, 4096, gExpected, lambdaContinue(*pSeqJob, 
                [&fileQueue, pSeqJob](WorkerThread&, intptr_t status, uint32_t length)
            {
                // oracle: verify write successful
                Tracer::LogInfo(H_ok, L"Write Finished");
                Audit::Assert((StatusCode)status == H_ok,
                    L"Failed async writing!");
                Audit::Assert(length == 4096,
                    L"write length error!");

                fileQueue->Read(0, 4096, gReadValue, lambdaContinue(*pSeqJob,
                    [](WorkerThread&, intptr_t status, uint32_t length)
                {
                    Tracer::LogInfo(H_ok, L"Read Finished");

                    // oracle: verify read successful
                    Audit::Assert((StatusCode)status == H_ok,
                        L"Failed async reading!");
                    Audit::Assert(length == 4096,
                        L"read length error!");
                    Audit::Assert(
                        0 == memcmp(gExpected, gReadValue, sizeof(gExpected)),
                        L"Mismatched value read from file!");

                    {
                        std::unique_lock<std::mutex> guard(glock);
                        gReadyExit.notify_all();
                    }
                }));
            }));

            {
                std::unique_lock<std::mutex> g(glock);
                gReadyExit.wait(g);
            }

            fileQueue.reset();
            pScheduler.reset();

            Tracer::DisposeLogging();
        }
    };
}
