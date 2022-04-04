// Copyright Epic Games, Inc. All Rights Reserved.

#include "FixedFPSPump.h"
#include "VideoSource.h"
#include "Settings.h"
#include "PixelStreamingFrameBuffer.h"
#include "PlayerSession.h"

namespace UE::PixelStreaming
{
	/*
	* ---------- FFixedFPSPump ----------------
	*/
	FFixedFPSPump::FFixedFPSPump()
		: Runnable()
	{
		if (Settings::CVarPixelStreamingDecoupleFrameRate.GetValueOnAnyThread())
		{
			PumpThread = FRunnableThread::Create(&Runnable, TEXT("Pixel Streaming Frame Pump"));
		}
	}

	FFixedFPSPump::~FFixedFPSPump()
	{
		Shutdown();
		if (Settings::CVarPixelStreamingDecoupleFrameRate.GetValueOnAnyThread())
		{
			PumpThread->Kill(true);
		}
	}

	void FFixedFPSPump::Shutdown()
	{
		Runnable.Stop();
	}

	void FFixedFPSPump::RegisterPumpable(rtc::scoped_refptr<FPixelStreamingPumpable> Pumpable)
	{
		checkf(Pumpable, TEXT("Cannot register a nullptr."));
		FScopeLock Guard(&Runnable.SourcesGuard);
		Runnable.Pumpables.Add(Pumpable.get(), Pumpable);
		Runnable.NextPumpEvent->Trigger();
	}

	void FFixedFPSPump::UnregisterPumpable(rtc::scoped_refptr<FPixelStreamingPumpable> Pumpable)
	{
		FScopeLock Guard(&Runnable.SourcesGuard);
		Runnable.Pumpables.Remove(Pumpable.get());
		Runnable.NextPumpEvent->Trigger();
	}

	void FFixedFPSPump::PumpAll()
	{
		Runnable.PumpAll();
	}

	/*
	* ------------- FPumpRunnable ---------------------
	*/
	FFixedFPSPump::FPumpRunnable::FPumpRunnable()
		: bIsRunning(false)
		, NextPumpEvent(FPlatformProcess::GetSynchEventFromPool(false))
	{
	}

	bool FFixedFPSPump::FPumpRunnable::Init()
	{
		return true;
	}

	void FFixedFPSPump::FPumpRunnable::PumpAll()
	{
		FScopeLock Guard(&SourcesGuard);
		const int32 FrameId = NextFrameId++;
		// Pump each video source
		TMap<FPixelStreamingPumpable*, rtc::scoped_refptr<FPixelStreamingPumpable>>::TIterator Iter = Pumpables.CreateIterator();
		for (; Iter; ++Iter)
		{
			FPixelStreamingPumpable* Pumpable = Iter.Value();
			if (Pumpable->HasOneRef())
			{
				Iter.RemoveCurrent();
				continue;
			}

			verifyf(Pumpable, TEXT("Pumpable was null"))
			if (Pumpable->IsReadyForPump())
			{
				Pumpable->OnPump(FrameId);
			}
		}
	}

	uint32 FFixedFPSPump::FPumpRunnable::Run()
	{
		LastPumpCycles = FPlatformTime::Cycles64();
		bIsRunning = true;

		while (bIsRunning)
		{
			// No sources, so just wait.
			if (Pumpables.Num() == 0)
			{
				NextPumpEvent->Wait();
			}

			PumpAll();

			// Sleep as long as we need for a constant FPS
			const uint64 EndCycles = FPlatformTime::Cycles64();
			const double DeltaMs = FPlatformTime::ToMilliseconds64(EndCycles - LastPumpCycles);
			const int32 FPS = Settings::CVarPixelStreamingWebRTCFps.GetValueOnAnyThread();
			const double FrameDeltaMs = 1000.0 / FPS;
			const double SleepMs = FrameDeltaMs - DeltaMs;
			LastPumpCycles = EndCycles;
			if (SleepMs > 0)
			{
				NextPumpEvent->Wait(SleepMs, false);
			}
		}
		return 0;
	}

	/* Tick() only occurs when engine is running in single threaded mode */
	void FFixedFPSPump::FPumpRunnable::Tick()
	{
		NextPumpEvent->Trigger();
		/*
		NOTE: When running single threaded, we will never be able to pump faster than the engine is rendering so the logic to do so is removed.
		*/
		if (Pumpables.Num() == 0)
		{
			// No sources, so just wait.
			return;
		}

		PumpAll();
	}

	void FFixedFPSPump::FPumpRunnable::Stop()
	{
		bIsRunning = false;
		NextPumpEvent->Trigger();
	}

	void FFixedFPSPump::FPumpRunnable::Exit()
	{
		bIsRunning = false;
		NextPumpEvent->Trigger();
	}
} // namespace UE::PixelStreaming