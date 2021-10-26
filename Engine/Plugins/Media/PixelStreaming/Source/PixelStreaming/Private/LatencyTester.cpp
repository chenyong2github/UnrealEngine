// Copyright Epic Games, Inc. All Rights Reserved.

#include "LatencyTester.h"
#include "PixelStreamingSettings.h"
#include <chrono>

FLatencyTester& FLatencyTester::GetInstance()
{
    static FLatencyTester Instance;
    return Instance;
}

bool FLatencyTester::IsTestRunning()
{
    return FLatencyTester::GetInstance().TestStage.load() != FLatencyTester::ELatencyTestStage::INACTIVE;
}

FLatencyTester::ELatencyTestStage FLatencyTester::GetTestStage()
{
    return FLatencyTester::GetInstance().TestStage.load();
}

// Start latency tester, this makes the RecordXXX functions do stuff (otherwise they no-op).
void FLatencyTester::Start(FPlayerId PlayerWhoTriggeredTest)
{
    bool bDisableLatencyTester = PixelStreamingSettings::CVarPixelStreamingDisableLatencyTester.GetValueOnAnyThread();
    if(bDisableLatencyTester)
    {
        return;
    }

    FLatencyTester::GetInstance().PlayerWhoTriggeredTest = PlayerWhoTriggeredTest;
    FLatencyTester::GetInstance().TestStage.store(FLatencyTester::ELatencyTestStage::STARTED);
    FLatencyTester::GetInstance().ReceiptTimeMs = 0;
    FLatencyTester::GetInstance().PreCaptureTimeMs = 0;
    FLatencyTester::GetInstance().PostCaptureTimeMs = 0;
    FLatencyTester::GetInstance().PreEncodeTimeMs = 0;
    FLatencyTester::GetInstance().PostEncodeTimeMs = 0;
    FLatencyTester::GetInstance().TransmissionTimeMs = 0;
}

bool FLatencyTester::RecordReceiptTime()
{
    if(FLatencyTester::GetInstance().GetTestStage() == FLatencyTester::ELatencyTestStage::STARTED)
    {
        FLatencyTester::GetInstance().ReceiptTimeMs = FLatencyTester::GetInstance().EpochMillisNow();
        FLatencyTester::GetInstance().TestStage.store(FLatencyTester::ELatencyTestStage::PRE_CAPTURE);
        return true;
    }
    return false;
}

bool FLatencyTester::RecordPreCaptureTime(uint32 FrameId)
{
    if(FLatencyTester::GetInstance().GetTestStage() == FLatencyTester::ELatencyTestStage::PRE_CAPTURE)
    {
        FLatencyTester::GetInstance().FrameId = FrameId;
        FLatencyTester::GetInstance().PreCaptureTimeMs = FLatencyTester::GetInstance().EpochMillisNow();
        FLatencyTester::GetInstance().TestStage.store(FLatencyTester::ELatencyTestStage::POST_CAPTURE);
        return true;
    }
    return false;
}

bool FLatencyTester::RecordPostCaptureTime(uint32 FrameId)
{
    if(FLatencyTester::GetInstance().GetTestStage() == FLatencyTester::ELatencyTestStage::POST_CAPTURE && FrameId == FLatencyTester::GetInstance().FrameId)
    {
        FLatencyTester::GetInstance().PostCaptureTimeMs = FLatencyTester::GetInstance().EpochMillisNow();
        FLatencyTester::GetInstance().TestStage.store(FLatencyTester::ELatencyTestStage::PRE_ENCODE);
        return true;
    }
    return false;
}

bool FLatencyTester::RecordPreEncodeTime(uint32 FrameId)
{
    if(FLatencyTester::GetInstance().GetTestStage() == FLatencyTester::ELatencyTestStage::PRE_ENCODE && FrameId == FLatencyTester::GetInstance().FrameId)
    {
        FLatencyTester::GetInstance().PreEncodeTimeMs = FLatencyTester::GetInstance().EpochMillisNow();
        FLatencyTester::GetInstance().TestStage.store(FLatencyTester::ELatencyTestStage::POST_ENCODE);
        return true;
    }
    return false;
}

bool FLatencyTester::RecordPostEncodeTime(uint32 FrameId)
{
    if(FLatencyTester::GetInstance().GetTestStage() == FLatencyTester::ELatencyTestStage::POST_ENCODE && FrameId == FLatencyTester::GetInstance().FrameId)
    {
        FLatencyTester::GetInstance().PostEncodeTimeMs = FLatencyTester::GetInstance().EpochMillisNow();
        FLatencyTester::GetInstance().TestStage.store(FLatencyTester::ELatencyTestStage::RESULTS_READY);
        return true;
    }
    return false;
}

// Ends latency testing, makes subsequent calls to RecordXXX functions a no-op.
bool FLatencyTester::End(FString& OutJSONString, FPlayerId& OutPlayerId)
{
    if(FLatencyTester::GetInstance().GetTestStage() == FLatencyTester::ELatencyTestStage::RESULTS_READY)
    {
        FLatencyTester::GetInstance().TransmissionTimeMs = FLatencyTester::GetInstance().EpochMillisNow();
        FLatencyTester::GetInstance().TestStage.store(FLatencyTester::ELatencyTestStage::INACTIVE);
        OutJSONString = FString::Printf( 
            TEXT( "{ \"ReceiptTimeMs\": %llu, \"PreCaptureTimeMs\": %llu, \"PostCaptureTimeMs\": %llu, \"PreEncodeTimeMs\": %llu, \"PostEncodeTimeMs\": %llu, \"TransmissionTimeMs\": %llu }" ), 
            FLatencyTester::GetInstance().ReceiptTimeMs,
            FLatencyTester::GetInstance().PreCaptureTimeMs,
            FLatencyTester::GetInstance().PostCaptureTimeMs,
            FLatencyTester::GetInstance().PreEncodeTimeMs,
            FLatencyTester::GetInstance().PostEncodeTimeMs,
            FLatencyTester::GetInstance().TransmissionTimeMs
        );
        OutPlayerId = FLatencyTester::GetInstance().PlayerWhoTriggeredTest;
        return true;
    }
    
    return false;
}

unsigned long long FLatencyTester::EpochMillisNow()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}