// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include <atomic>

// Performs a latency test of one frame passing through Pixel Streaming
// from capture, to encode, to transmit
class FLatencyTester 
{
    public:

        enum ELatencyTestStage 
        {
            INACTIVE = 0,
            STARTED = 1,
            PRE_CAPTURE = 2,
            POST_CAPTURE = 3,
            PRE_ENCODE = 4,
            POST_ENCODE = 5,
            RESULTS_READY = 6
        };

        static ELatencyTestStage GetTestStage();
        // Start latency tester, this makes the RecordXXX functions do stuff (otherwise they no-op).
        static void Start();
        static bool RecordReceiptTime();
        static bool RecordPreCaptureTime(uint32 FrameId);
        static bool RecordPostCaptureTime(uint32 FrameId);
        static bool RecordPreEncodeTime(uint32 FrameId);
        static bool RecordPostEncodeTime(uint32 FrameId);
        static bool IsTestRunning();
        // Ends latency testing, resets all the internally recorded timings and makes subsequent calls to RecordXXX functions a no-op.
        static bool End(FString& OutJSONString);

    private:
        static FLatencyTester& GetInstance();

        //Private constructor, only static methods are exposed.
        FLatencyTester(){}
        unsigned long long EpochMillisNow();

        unsigned long long ReceiptTimeMs = 0;
        unsigned long long PreCaptureTimeMs = 0;
        unsigned long long PostCaptureTimeMs = 0;
        unsigned long long PreEncodeTimeMs = 0;
        unsigned long long PostEncodeTimeMs = 0;
        uint32 FrameId = 0;
        std::atomic<ELatencyTestStage> TestStage = ELatencyTestStage::INACTIVE;
};