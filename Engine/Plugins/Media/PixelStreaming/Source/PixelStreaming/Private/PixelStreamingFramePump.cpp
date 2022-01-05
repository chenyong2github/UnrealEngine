// Copyright Epic Games, Inc. All Rights Reserved.
#include "PixelStreamingFramePump.h"

#include "PlayerVideoSource.h"
#include "PixelStreamingSettings.h"
#include "PixelStreamingFrameSource.h"
#include "PixelStreamingFrameBuffer.h"
#include "PlayerSession.h"

FPixelStreamingFramePump::FPixelStreamingFramePump(FPixelStreamingFrameSource* InFrameSource)
	: FrameSource(InFrameSource)
	, PlayersChangedEvent(FPlatformProcess::GetSynchEventFromPool(false))
	, NextPumpEvent(FPlatformProcess::GetSynchEventFromPool(false))
{
	PumpThread = MakeUnique<FThread>(TEXT("PumpThread"), [this]() { PumpLoop(); });
}

FPixelStreamingFramePump::~FPixelStreamingFramePump()
{
	bThreadRunning = false;
	PlayersChangedEvent->Trigger();
	NextPumpEvent->Trigger();
	PumpThread->Join();
}

FPlayerVideoSource* FPixelStreamingFramePump::CreatePlayerVideoSource(FPlayerId PlayerId, int Flags)
{
	FPlayerVideoSource* NewSource = nullptr;

	if (PlayerVideoSources.Num() != 0)
	{
		FScopeLock Guard(&ExtraSourcesGuard);
		ExtraVideoSources.Add(MakeUnique<FPlayerVideoSource>(PlayerId));
		NewSource = ExtraVideoSources[ExtraVideoSources.Num() - 1].Get();
	}
	else
	{
		FScopeLock Guard(&SourcesGuard);
		PlayerVideoSources.Add(PlayerId, MakeUnique<FPlayerVideoSource>(PlayerId));
		NewSource = PlayerVideoSources[PlayerId].Get();
	}

	if ((Flags & PixelStreamingProtocol::EPixelStreamingPlayerFlags::PSPFlag_IsSFU) != PixelStreamingProtocol::EPixelStreamingPlayerFlags::PSPFlag_None)
	{
		ElevatedPlayerId = PlayerId;
	}

	PlayersChangedEvent->Trigger();
	return NewSource;
}

void FPixelStreamingFramePump::RemovePlayerVideoSource(FPlayerId PlayerId)
{
	FScopeLock Guard(&SourcesGuard);
	PlayerVideoSources.Remove(PlayerId);

	if (PlayerId == ElevatedPlayerId)
	{
		ElevatedPlayerId = INVALID_PLAYER_ID;
	}
}

void FPixelStreamingFramePump::SetQualityController(FPlayerId PlayerId)
{
	FScopeLock Guard(&SourcesGuard);
	QualityControllerId = PlayerId;
	PlayersChangedEvent->Trigger();
}

void FPixelStreamingFramePump::PumpLoop()
{
	uint64 LastCycles = FPlatformTime::Cycles64();

	while (bThreadRunning)
	{
		bool IdleSleep = true;

		if (FrameSource->IsAvailable())
		{
			if (ExtraVideoSources.Num() > 0)
			{
				FScopeLock Guard0(&ExtraSourcesGuard);
				FScopeLock Guard1(&SourcesGuard);

				const int64 TimestampUs = rtc::TimeMicros();

				// build a frame to pass to the quality controller source
				rtc::scoped_refptr<FPixelStreamingInitializeFrameBuffer> EncoderFrameBuffer = new rtc::RefCountedObject<FPixelStreamingInitializeFrameBuffer>(FrameSource);

				webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder()
					.set_video_frame_buffer(EncoderFrameBuffer)
					.set_timestamp_us(TimestampUs)
					.set_rotation(webrtc::VideoRotation::kVideoRotation_0)
					.set_id(0)
					.build();

				for (auto& ExtraSource : ExtraVideoSources)
				{
					ExtraSource->OnFrameReady(Frame);
					if (ExtraSource->IsInitialised())
					{
						PlayerVideoSources.Add(ExtraSource->GetPlayerId(), MoveTemp(ExtraSource));
					}
				}

				ExtraVideoSources.RemoveAll([](auto& ExtraSource) { return !ExtraSource; });
			}
		}

		const FPlayerId PumpPlayerId = (ElevatedPlayerId != INVALID_PLAYER_ID) ? ElevatedPlayerId : QualityControllerId;

		if (PumpPlayerId != INVALID_PLAYER_ID)
		{
			FScopeLock Guard(&SourcesGuard); // lock here so the quality controller cant be removed after the check
			if (PlayerVideoSources.Contains(PumpPlayerId))
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
					PlayerVideoSources[PumpPlayerId]->OnFrameReady(Frame);
				}
				IdleSleep = false;
			}
		}

		if (IdleSleep)
		{
			PlayersChangedEvent->Wait();
		}
		else
		{
			// Sleep as long as we need for a constant FPS
			const uint64 EndCycles = FPlatformTime::Cycles64();
			const double DeltaMs = FPlatformTime::ToMilliseconds64(EndCycles - LastCycles);
			const int32 FPS = PixelStreamingSettings::CVarPixelStreamingWebRTCFps.GetValueOnAnyThread();
			const double FrameDeltaMs = 1000.0 / FPS;
			const double SleepMs = FrameDeltaMs - DeltaMs;
			LastCycles = EndCycles;

			if (SleepMs > 0)
			{
				NextPumpEvent->Wait(SleepMs, false);
			}
		}
	}
}
