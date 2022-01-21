// Copyright Epic Games, Inc. All Rights Reserved.

#include "FixedFPSPump.h"
#include "VideoSource.h"
#include "Settings.h"
#include "TextureSource.h"
#include "FrameBuffer.h"
#include "PlayerSession.h"

/*
* ---------- FFixedFPSPump ----------------
*/

UE::PixelStreaming::FFixedFPSPump* UE::PixelStreaming::FFixedFPSPump::Instance = nullptr;

UE::PixelStreaming::FFixedFPSPump* UE::PixelStreaming::FFixedFPSPump::Get()
{
	checkf(Instance, TEXT("You should not try to Get() and instance of the pump before it has been constructed somewhere yet."));
	return UE::PixelStreaming::FFixedFPSPump::Instance;
}

UE::PixelStreaming::FFixedFPSPump::FFixedFPSPump()
	: NextPumpEvent(FPlatformProcess::GetSynchEventFromPool(false))
{
	PumpThread = MakeUnique<FThread>(TEXT("PumpThread"), [this]() { PumpLoop(); });
	UE::PixelStreaming::FFixedFPSPump::Instance = this;
}

UE::PixelStreaming::FFixedFPSPump::~FFixedFPSPump()
{
	Shutdown();
	PumpThread->Join();
}

void UE::PixelStreaming::FFixedFPSPump::Shutdown()
{
	bThreadRunning = false;
	NextPumpEvent->Trigger();
}

void UE::PixelStreaming::FFixedFPSPump::UnregisterVideoSource(FPixelStreamingPlayerId PlayerId)
{
	FScopeLock Guard(&SourcesGuard);
	VideoSources.Remove(PlayerId);
	NextPumpEvent->Trigger();
}

void UE::PixelStreaming::FFixedFPSPump::RegisterVideoSource(FPixelStreamingPlayerId PlayerId, UE::PixelStreaming::IPumpedVideoSource* Source)
{
	checkf(Source, TEXT("Cannot register a nullptr VideoSource."));
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
			TMap<FPixelStreamingPlayerId, IPumpedVideoSource*>::TIterator Iter = VideoSources.CreateIterator();
			for(; Iter; ++Iter)
			{

				IPumpedVideoSource* VideoSource = Iter.Value();

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
