// Copyright Epic Games, Inc. All Rights Reserved.

#include "Quartz/AudioMixerClockManager.h"
#include "AudioMixerDevice.h"
#include "Misc/ScopeLock.h"

namespace Audio
{
	FQuartzClockManager::FQuartzClockManager(Audio::FMixerDevice* InOwner)
	: MixerDevice(InOwner)
	{
	}

	FQuartzClockManager::~FQuartzClockManager()
	{
	}

	void FQuartzClockManager::Update(int32 NumFramesUntilNextUpdate)
	{
		// this function should only be called on the Audio Render Thread
		// (original intent was for this to be called only by the owning FMixerDevice)
		checkSlow(MixerDevice->IsAudioRenderingThread());

		TickClocks(NumFramesUntilNextUpdate);
	}

	FQuartzClock* FQuartzClockManager::GetOrCreateClock(const FName& InClockName, const FQuartzClockSettings& InClockSettings, bool bOverrideTickRateIfClockExists)
	{
		// since we are returning a key, we need to perform this function here w/ a critical section
		FScopeLock Lock(&ActiveClocksMutationCritSec);

		// make a copy of the Settings
		FQuartzClockSettings NewSettings = InClockSettings;

		// See if this clock already exists
		FQuartzClock* Clock = FindClock(InClockName);

		if (Clock)
		{
			if (bOverrideTickRateIfClockExists && !Clock->DoesMatchSettings(NewSettings))
			{
				UE_LOG(LogAudioQuartz, Display, TEXT("Overriding Tick Rate on Clock: %s"), *Clock->GetName().ToString());
				Clock->ChangeTimeSignature(NewSettings.TimeSignature);
			}

			return Clock;
		}

		// doesn't exist, create new clock
		return &ActiveClocks.Emplace_GetRef(FQuartzClock(InClockName, NewSettings, this));
	}

	bool FQuartzClockManager::DoesClockExist(const FName& InClockName)
	{
		FScopeLock Lock(&ActiveClocksMutationCritSec);

		return !!FindClock(InClockName);
	}

	void FQuartzClockManager::RemoveClock(const FName& InName)
	{
		if (!MixerDevice->IsAudioRenderingThread())
		{
			MixerDevice->AudioRenderThreadCommand([this, InName]()
			{
				RemoveClock(InName);
			});

			return;
		}


		int32 NumClocks = ActiveClocks.Num();
		for (int32 i = 0; i < NumClocks; ++i)
		{
			if (ActiveClocks[i].GetName() == InName)
			{
				ActiveClocks.RemoveAtSwap(i);
			}
		}

	}

	FQuartzClockTickRate FQuartzClockManager::GetTickRateForClock(const FName& InName)
	{
		FScopeLock Lock(&ActiveClocksMutationCritSec);

		FQuartzClock* Clock = FindClock(InName);

		if (Clock)
		{
			return Clock->GetTickRate();
		}

		return FQuartzClockTickRate();
	}

	void FQuartzClockManager::SetTickRateForClock(const FQuartzClockTickRate& InNewTickRate, const FName& InName)
	{
		if (!MixerDevice->IsAudioRenderingThread())
		{
			MixerDevice->AudioRenderThreadCommand([this, InNewTickRate, InName]()
			{
				SetTickRateForClock(InNewTickRate, InName);
			});

			return;
		}

		// Anything below is being executed on the Audio Render Thread
		FQuartzClock* Clock = FindClock(InName);
		if (Clock)
		{
			Clock->ChangeTickRate(InNewTickRate);
		}
	}

	void FQuartzClockManager::ResumeClock(const FName& InName)
	{
		if (!MixerDevice->IsAudioRenderingThread())
		{
			MixerDevice->AudioRenderThreadCommand([this, InName]()
			{
				ResumeClock(InName);
			});

			return;
		}

		// Anything below is being executed on the Audio Render Thread
		FQuartzClock* Clock = FindClock(InName);
		if (Clock)
		{
			Clock->Resume();
		}
	}

	void FQuartzClockManager::PauseClock(const FName& InName)
	{
		if (!MixerDevice->IsAudioRenderingThread())
		{
			MixerDevice->AudioRenderThreadCommand([this, InName]()
			{
				PauseClock(InName);
			});

			return;
		}

		// Anything below is being executed on the Audio Render Thread

		FQuartzClock* Clock = FindClock(InName);
		if (Clock)
		{
			Clock->Pause();
		}
	}

	void FQuartzClockManager::Flush()
	{
		int32 NumClocks = ActiveClocks.Num();
		for (int32 i = NumClocks - 1; i >= 0; --i)
		{
			if (!ActiveClocks[i].IgnoresFlush())
			{
				ActiveClocks[i].Shutdown();
				ActiveClocks.RemoveAtSwap(i);
			}
		}
	}

	void FQuartzClockManager::Shutdown()
	{
		for (auto& Clock : ActiveClocks)
		{
			Clock.Shutdown();
		}
	}

	FQuartzQuantizedCommandHandle FQuartzClockManager::AddCommandToClock(FQuartzQuantizedCommandInitInfo& InQuantizationCommandInitInfo)
	{
		checkSlow(MixerDevice->IsAudioRenderingThread());

		if (FQuartzClock* Clock = FindClock(InQuantizationCommandInitInfo.ClockName))
		{
			// pass the quantized command to it's clock
			InQuantizationCommandInitInfo.SetOwningClockPtr(Clock);
			InQuantizationCommandInitInfo.QuantizedCommandPtr->OnQueued(InQuantizationCommandInitInfo);
			Clock->AddQuantizedCommand(InQuantizationCommandInitInfo.QuantizationBoundary, InQuantizationCommandInitInfo.QuantizedCommandPtr);

			// initialize the handle the audio source can use to cancel this quantized command
			FQuartzQuantizedCommandHandle Handle;
			Handle.OwningClockName = InQuantizationCommandInitInfo.ClockName;
			Handle.CommandPtr = InQuantizationCommandInitInfo.QuantizedCommandPtr;
			Handle.MixerDevice = MixerDevice;

			return Handle;
		}

		return {};
	}

	void FQuartzClockManager::SubscribeToTimeDivision(FName InClockName, MetronomeCommandQueuePtr InListenerQueue, EQuartzCommandQuantization InQuantizationBoundary)
	{
		if (!MixerDevice->IsAudioRenderingThread())
		{
			MixerDevice->AudioRenderThreadCommand([this, InClockName, InListenerQueue, InQuantizationBoundary]()
			{
				SubscribeToTimeDivision(InClockName, InListenerQueue, InQuantizationBoundary);
			});

			return;
		}

		// Anything below is being executed on the Audio Render Thread

		FQuartzClock* Clock = FindClock(InClockName);
		if (Clock)
		{
			Clock->SubscribeToTimeDivision(InListenerQueue, InQuantizationBoundary);
		}
	}

	void FQuartzClockManager::SubscribeToAllTimeDivisions(FName InClockName, MetronomeCommandQueuePtr InListenerQueue)
	{
		if (!MixerDevice->IsAudioRenderingThread())
		{
			MixerDevice->AudioRenderThreadCommand([this, InClockName, InListenerQueue]()
			{
				SubscribeToAllTimeDivisions(InClockName, InListenerQueue);
			});

			return;
		}

		// Anything below is being executed on the Audio Render Thread

		FQuartzClock* Clock = FindClock(InClockName);
		if (Clock)
		{
			Clock->SubscribeToAllTimeDivisions(InListenerQueue);
		}
	}

	void FQuartzClockManager::UnsubscribeFromTimeDivision(FName InClockName, MetronomeCommandQueuePtr InListenerQueue, EQuartzCommandQuantization InQuantizationBoundary)
	{
		if (!MixerDevice->IsAudioRenderingThread())
		{
			MixerDevice->AudioRenderThreadCommand([this, InClockName, InListenerQueue, InQuantizationBoundary]()
			{
				UnsubscribeFromTimeDivision(InClockName, InListenerQueue, InQuantizationBoundary);
			});

			return;
		}

		// Anything below is being executed on the Audio Render Thread

		FQuartzClock* Clock = FindClock(InClockName);
		if (Clock)
		{
			Clock->UnsubscribeFromTimeDivision(InListenerQueue, InQuantizationBoundary);
		}
	}

	void FQuartzClockManager::UnsubscribeFromAllTimeDivisions(FName InClockName, MetronomeCommandQueuePtr InListenerQueue)
	{
		if (!MixerDevice->IsAudioRenderingThread())
		{
			MixerDevice->AudioRenderThreadCommand([this, InClockName, InListenerQueue]()
			{
				UnsubscribeFromAllTimeDivisions(InClockName, InListenerQueue);
			});

			return;
		}

		// Anything below is being executed on the Audio Render Thread

		FQuartzClock* Clock = FindClock(InClockName);
		if (Clock)
		{
			Clock->UnsubscribeFromAllTimeDivisions(InListenerQueue);
		}
	}

	bool FQuartzClockManager::CancelCommandOnClock(FName InOwningClockName, TSharedPtr<IQuartzQuantizedCommand> InCommandPtr)
	{
		FQuartzClock* Clock = FindClock(InOwningClockName);

		if (Clock && InCommandPtr)
		{
			return Clock->CancelQuantizedCommand(InCommandPtr);
		}

		return false;
	}

	void FQuartzClockManager::TickClocks(int32 NumFramesToTick)
	{
		// This function should only be called on the Audio Render Thread
		checkSlow(MixerDevice->IsAudioRenderingThread());

		for (auto& Clock : ActiveClocks)
		{
			Clock.Tick(NumFramesToTick);
		}
	}

	FQuartzClock* FQuartzClockManager::FindClock(const FName& InName)
	{
		// the function calling this should be be on the audio rendering thread or have acquired the lock
		checkSlow(MixerDevice->IsAudioRenderingThread() || ActiveClocksMutationCritSec.TryLock());

		for (auto& Clock : ActiveClocks)
		{
			if (Clock.GetName() == InName)
			{
				ActiveClocksMutationCritSec.Unlock();
				return &Clock;
			}
		}

		// didn't exist
		ActiveClocksMutationCritSec.Unlock();
		return nullptr;
	}
} // namespace Audio
