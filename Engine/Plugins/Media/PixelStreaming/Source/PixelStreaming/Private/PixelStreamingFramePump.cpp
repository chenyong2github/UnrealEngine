// Copyright Epic Games, Inc. All Rights Reserved.
#include "PixelStreamingFramePump.h"

#include "PlayerVideoSource.h"
#include "PixelStreamingSettings.h"
#include "PixelStreamingFrameSource.h"
#include "PixelStreamingFrameBuffer.h"

FPixelStreamingFramePump::FPixelStreamingFramePump(FPixelStreamingFrameSource* InFrameSource)
:FrameSource(InFrameSource)
,QualityControllerEvent(FPlatformProcess::GetSynchEventFromPool(false))
,NextPumpEvent(FPlatformProcess::GetSynchEventFromPool(false))
{
	const int32 FPS = PixelStreamingSettings::CVarPixelStreamingWebRTCFps.GetValueOnAnyThread();
	FrameDeltaMs = 1000.0 / FPS;
	PumpThread = MakeUnique<FThread>(TEXT("PumpThread"), [this]() { PumpLoop(); });
}

FPixelStreamingFramePump::~FPixelStreamingFramePump()
{
    bThreadRunning = false;
    QualityControllerEvent->Trigger();
    NextPumpEvent->Trigger();
    PumpThread->Join();
}

FPlayerVideoSource* FPixelStreamingFramePump::CreatePlayerVideoSource(FPlayerId PlayerId)
{
	FScopeLock Guard(&SourcesGuard);
	PlayerVideoSources.Add(PlayerId, MakeUnique<FPlayerVideoSource>());
	QualityControllerEvent->Trigger();
	return PlayerVideoSources[PlayerId].Get();
}

void FPixelStreamingFramePump::RemovePlayerVideoSource(FPlayerId PlayerId)
{
	FScopeLock Guard(&SourcesGuard);
	PlayerVideoSources.Remove(PlayerId);
}

void FPixelStreamingFramePump::SetQualityController(FPlayerId PlayerId)
{
	FScopeLock Guard(&SourcesGuard);
	QualityControllerId = PlayerId;
	QualityControllerEvent->Trigger();
}

void FPixelStreamingFramePump::PumpLoop()
{
	uint64 LastCycles = FPlatformTime::Cycles64();

	while (bThreadRunning)
	{
		bool IdleSleep = QualityControllerId == INVALID_PLAYER_ID;
		if (!IdleSleep)
		{
			FScopeLock Guard(&SourcesGuard); // lock here so the quality controller cant be removed after the check
			if (PlayerVideoSources.Contains(QualityControllerId))
			{
				if (FrameSource->IsAvailable()) // dont trigger a sleep if this fails since we dont trigger the event for this changing
				{
					const int64 TimestampUs = rtc::TimeMicros();
					const int32 FrameId = NextFrameId++;

					// build a frame to pass to the quality controller source
					rtc::scoped_refptr<FPixelStreamingSimulcastFrameBuffer> EncoderFrameBuffer = new rtc::RefCountedObject<FPixelStreamingSimulcastFrameBuffer>(FrameSource);

					webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder()
						.set_video_frame_buffer(EncoderFrameBuffer)
						.set_timestamp_us(TimestampUs)
						.set_rotation(webrtc::VideoRotation::kVideoRotation_0)
						.set_id(FrameId)
						.build();
					
					// only send to quality controller (if it still exists. it might have been removed since the last check)
					PlayerVideoSources[QualityControllerId]->OnFrameReady(Frame);
				}
			}
			else
			{
				// Quality controller doesn't exist. Idle.
				IdleSleep = true;
			}
		}

		if (IdleSleep)
		{
			QualityControllerEvent->Wait();
		}
		else
		{
	        // Sleep as long as we need for a constant FPS
	        const uint64 EndCycles = FPlatformTime::Cycles64();
	        const double DeltaMs = FPlatformTime::ToMilliseconds64(EndCycles - LastCycles);
	        const double SleepMs = FrameDeltaMs - DeltaMs;
	        LastCycles = EndCycles;

	        if (SleepMs > 0)
	        {
	        	NextPumpEvent->Wait(SleepMs, false);
	        }
		}
	}
}
