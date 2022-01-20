// Copyright Epic Games, Inc. All Rights Reserved.

#include "FixedFPSPump.h"
#include "VideoSource.h"
#include "Settings.h"
#include "TextureSource.h"
#include "FrameBuffer.h"
#include "PlayerSession.h"

UE::PixelStreaming::FFixedFPSPump& UE::PixelStreaming::FFixedFPSPump::Get()
{
	static UE::PixelStreaming::FFixedFPSPump Pump = UE::PixelStreaming::FFixedFPSPump();
	return Pump;
}

UE::PixelStreaming::FFixedFPSPump::FFixedFPSPump()
	: NextPumpEvent(FPlatformProcess::GetSynchEventFromPool(false))
{
	PumpThread = MakeUnique<FThread>(TEXT("PumpThread"), [this]() { PumpLoop(); });
}

UE::PixelStreaming::FFixedFPSPump::~FFixedFPSPump()
{
	bThreadRunning = false;
	NextPumpEvent->Trigger();
	PumpThread->Join();
}

void UE::PixelStreaming::FFixedFPSPump::UnregisterVideoSource(FPixelStreamingPlayerId PlayerId)
{
	FScopeLock Guard(&SourcesGuard);
	VideoSources.Remove(PlayerId);
	NextPumpEvent->Trigger();
}

void UE::PixelStreaming::FFixedFPSPump::RegisterVideoSource(FPixelStreamingPlayerId PlayerId, rtc::scoped_refptr<UE::PixelStreaming::IPumpedVideoSource> Source)
{
	FScopeLock Guard(&SourcesGuard);
	VideoSources.Add(PlayerId, Source);
	NextPumpEvent->Trigger();
}

void UE::PixelStreaming::FFixedFPSPump::PumpLoop()
{
	uint64 LastCycles = FPlatformTime::Cycles64();

	while (bThreadRunning)
	{
		// No sources, so just wait.
		if (VideoSources.Num() == 0)
		{
			NextPumpEvent->Wait();
		}

		{
			FScopeLock Guard(&SourcesGuard);

			const int32 FrameId = NextFrameId++;

			// Pump each video source
			TMap<FPixelStreamingPlayerId, rtc::scoped_refptr<UE::PixelStreaming::IPumpedVideoSource>>::TIterator Iter = VideoSources.CreateIterator();
			for(; Iter; ++Iter)
			{
				rtc::scoped_refptr<UE::PixelStreaming::IPumpedVideoSource>& VideoSource = Iter.Value();

				// If the pump is the last thing holding this video source, remove the video source.
				if(VideoSource->HasOneRef())
				{
					Iter.RemoveCurrent();
					continue;
				}
				
				if(VideoSource->IsReadyForPump())
				{
					VideoSource->OnPump(FrameId);
				}
			}
		}

		// Sleep as long as we need for a constant FPS
		const uint64 EndCycles	  = FPlatformTime::Cycles64();
		const double DeltaMs	  = FPlatformTime::ToMilliseconds64(EndCycles - LastCycles);
		const int32 FPS			  = UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCFps.GetValueOnAnyThread();
		const double FrameDeltaMs = 1000.0 / FPS;
		const double SleepMs	  = FrameDeltaMs - DeltaMs;
		LastCycles				  = EndCycles;

		if (SleepMs > 0)
		{
			NextPumpEvent->Wait(SleepMs, false);
		}
	}
}
