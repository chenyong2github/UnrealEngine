// Copyright Epic Games, Inc. All Rights Reserved.

#include "Quartz/AudioMixerClock.h"
#include "Quartz/AudioMixerClockManager.h"
#include "AudioMixerSourceManager.h"


static float HeadlessClockSampleRateCvar = 100000.f;
FAutoConsoleVariableRef CVarHeadlessClockSampleRate(
	TEXT("au.Quartz.HeadlessClockSampleRate"),
	HeadlessClockSampleRateCvar,
	TEXT("Sample rate to use for Quartz Clocks/Metronomes when no Mixer Device is present.\n")
	TEXT("0: Not Enabled, 1: Enabled"),
	ECVF_Default);

namespace Audio
{
	FQuartzClock::FQuartzClock(const FName& InName, const FQuartzClockSettings& InClockSettings, FQuartzClockManager* InOwningClockManagerPtr)
		: Metronome(InClockSettings.TimeSignature)
		, OwningClockManagerPtr(InOwningClockManagerPtr)
		, Name(InName)
		, bIsRunning(false)
		, bIgnoresFlush(InClockSettings.bIgnoreLevelChange)
	{

		FMixerDevice* MixerDevice = GetMixerDevice();

		if (MixerDevice)
		{
			Metronome.SetSampleRate(MixerDevice->GetSampleRate());
		}
		else
		{
			Metronome.SetSampleRate(HeadlessClockSampleRateCvar);
		}
	}

	FQuartzClock::~FQuartzClock()
	{
		Shutdown();
	}

	void FQuartzClock::ChangeTickRate(FQuartzClockTickRate InNewTickRate, int32 NumFramesLeft)
	{
		FMixerDevice* MixerDevice = GetMixerDevice();

		if (MixerDevice)
		{
			InNewTickRate.SetSampleRate(MixerDevice->GetSampleRate());
		}
		else
		{
			InNewTickRate.SetSampleRate(HeadlessClockSampleRateCvar);
		}

		Metronome.SetTickRate(InNewTickRate, NumFramesLeft);
		FQuartzClockTickRate CurrentTickRate = Metronome.GetTickRate();

		// ratio between new and old rates
		const float Ratio = static_cast<float>(InNewTickRate.GetFramesPerTick()) / static_cast<float>(CurrentTickRate.GetFramesPerTick());

		// adjust time-till-fire for existing commands
		for (auto& Command : PendingCommands)
		{
			Command.NumFramesUntilExec = NumFramesLeft + Ratio * (Command.NumFramesUntilExec - NumFramesLeft);
		}

		for (auto& Command : ClockAlteringPendingCommands)
		{
			Command.NumFramesUntilExec = NumFramesLeft + Ratio * (Command.NumFramesUntilExec - NumFramesLeft);
		}
	}

	void FQuartzClock::ChangeTimeSignature(const FQuartzTimeSignature& InNewTimeSignature)
	{
		// TODO: what does this do to pending events waiting for the beat? (maybe nothing is reasonable?)
		Metronome.SetTimeSignature(InNewTimeSignature);
	}

	void FQuartzClock::Resume()
	{
		if (bIsRunning == false)
		{
			for (auto& Command : PendingCommands)
			{
				// Update countdown time to each quantized command
				Command.Command->OnClockStarted();
			}

			for (auto& Command : ClockAlteringPendingCommands)
			{
				// Update countdown time to each quantized command
				Command.Command->OnClockStarted();
			}
		}

		bIsRunning = true;
	}

	void FQuartzClock::Stop(bool CancelPendingEvents)
	{
		bIsRunning = false;
		Metronome.ResetTransport();
		TickDelayLengthInFrames = 0;

		if (CancelPendingEvents)
		{
			for (auto& Command : PendingCommands)
			{
				Command.Command->Cancel();
			}

			for (auto& Command : ClockAlteringPendingCommands)
			{
				Command.Command->Cancel();
			}
		}
	}

	void FQuartzClock::Pause()
	{
		if (bIsRunning)
		{
			for (auto& Command : PendingCommands)
			{
				// Update countdown time to each quantized command
				Command.Command->OnClockPaused();
			}

			for (auto& Command : ClockAlteringPendingCommands)
			{
				// Update countdown time to each quantized command
				Command.Command->OnClockPaused();
			}
		}

		bIsRunning = false;
	}

	void FQuartzClock::Restart(bool bPause)
	{
		bIsRunning = !bPause;
		TickDelayLengthInFrames = 0;
	}

	void FQuartzClock::Shutdown()
	{
		for (auto& PendingCommand : PendingCommands)
		{
			PendingCommand.Command->Cancel();
		}

		for (auto& PendingCommand : ClockAlteringPendingCommands)
		{
			PendingCommand.Command->Cancel();
		}

		PendingCommands.Reset();
		ClockAlteringPendingCommands.Reset();
	}

	void FQuartzClock::LowResolutionTick(float InDeltaTimeSeconds)
	{
		Tick(static_cast<int32>(InDeltaTimeSeconds * Metronome.GetTickRate().GetSampleRate()));
	}

	void FQuartzClock::Tick(int32 InNumFramesUntilNextTick)
	{
		if (!bIsRunning)
		{
			return;
		}

		if (TickDelayLengthInFrames >= InNumFramesUntilNextTick)
		{
			TickDelayLengthInFrames -= InNumFramesUntilNextTick;
			return;
		}

		const int32 FramesOfLatency = (ThreadLatencyInMilliseconds / 1000) * Metronome.GetTickRate().GetSampleRate();

		if (TickDelayLengthInFrames == 0)
		{
			TickInternal(InNumFramesUntilNextTick, ClockAlteringPendingCommands, FramesOfLatency); // (process things like BPM changes first)
			TickInternal(InNumFramesUntilNextTick, PendingCommands, FramesOfLatency);
		}
		else
		{
			TickInternal(TickDelayLengthInFrames, ClockAlteringPendingCommands, FramesOfLatency);
			TickInternal(TickDelayLengthInFrames, PendingCommands, FramesOfLatency);

			TickInternal(InNumFramesUntilNextTick - TickDelayLengthInFrames, ClockAlteringPendingCommands, FramesOfLatency, TickDelayLengthInFrames);
			TickInternal(InNumFramesUntilNextTick - TickDelayLengthInFrames, PendingCommands, FramesOfLatency, TickDelayLengthInFrames);
		}


		Metronome.Tick(InNumFramesUntilNextTick, FramesOfLatency);
	}

	void FQuartzClock::TickInternal(int32 InNumFramesUntilNextTick, TArray<PendingCommand>& CommandsToTick, int32 FramesOfLatency, int32 FramesOfDelay)
	{
		bool bHaveCommandsToRemove = false;

		// Update all pending commands
		for (PendingCommand& PendingCommand : CommandsToTick)
		{
			// Time to notify game thread?
			if (PendingCommand.NumFramesUntilExec < FramesOfLatency)
			{
				PendingCommand.Command->AboutToStart();
			}

			// Time To execute?
			if (PendingCommand.NumFramesUntilExec < InNumFramesUntilNextTick)
			{
				PendingCommand.Command->OnFinalCallback(PendingCommand.NumFramesUntilExec + FramesOfDelay);
				PendingCommand.Command.Reset();
				bHaveCommandsToRemove = true;
			}
			else // not yet executing
			{
				PendingCommand.NumFramesUntilExec -= InNumFramesUntilNextTick;
			}
		}

		// clean up executed commands
		if (bHaveCommandsToRemove)
		{
			for (int32 i = CommandsToTick.Num() - 1; i >= 0; --i)
			{
				if (!CommandsToTick[i].Command.IsValid())
				{
					CommandsToTick.RemoveAtSwap(i);
				}
			}
		}
	}

	void FQuartzClock::SetSampleRate(float InNewSampleRate)
	{
		if (FMath::IsNearlyEqual(InNewSampleRate, Metronome.GetTickRate().GetSampleRate()))
		{
			return;
		}

		// update Tick Rate
		Metronome.SetSampleRate(InNewSampleRate);

		// TODO: update the deadlines of all our new events
	}

	bool FQuartzClock::IgnoresFlush()
	{
		return bIgnoresFlush;
	}

	bool FQuartzClock::DoesMatchSettings(const FQuartzClockSettings& InClockSettings) const
	{
		return Metronome.GetTimeSignature() == InClockSettings.TimeSignature;
	}

	void FQuartzClock::SubscribeToTimeDivision(MetronomeCommandQueuePtr InListenerQueue, EQuartzCommandQuantization InQuantizationBoundary)
	{
		Metronome.SubscribeToTimeDivision(InListenerQueue, InQuantizationBoundary);
	}

	void FQuartzClock::SubscribeToAllTimeDivisions(MetronomeCommandQueuePtr InListenerQueue)
	{
		Metronome.SubscribeToAllTimeDivisions(InListenerQueue);
	}

	void FQuartzClock::UnsubscribeFromTimeDivision(MetronomeCommandQueuePtr InListenerQueue, EQuartzCommandQuantization InQuantizationBoundary)
	{
		Metronome.UnsubscribeFromTimeDivision(InListenerQueue, InQuantizationBoundary);
	}

	void FQuartzClock::UnsubscribeFromAllTimeDivisions(MetronomeCommandQueuePtr InListenerQueue)
	{
		Metronome.UnsubscribeFromAllTimeDivisions(InListenerQueue);
	}

	void FQuartzClock::AddQuantizedCommand(FQuartzQuantizationBoundary InQuantizationBondary, TSharedPtr<IQuartzQuantizedCommand> InNewEvent)
	{
		if (!ensure(InNewEvent.IsValid()))
		{
			return;
		}

		// if this is unquantized, execute immediately (even if the clock is paused)
		if (InQuantizationBondary.Quantization == EQuartzCommandQuantization::None)
		{
			InNewEvent->AboutToStart();
			InNewEvent->OnFinalCallback(0);
			return;
		}

		// get number of frames until event (assuming we are at frame 0)
		int32 FramesUntilExec = Metronome.GetFramesUntilBoundary(InQuantizationBondary);

		// add to pending commands list, execute OnQueued()
		if (InNewEvent->IsClockAltering())
		{
			ClockAlteringPendingCommands.Emplace(PendingCommand(MoveTemp(InNewEvent), FramesUntilExec));
		}
		else
		{
			PendingCommands.Emplace(PendingCommand(MoveTemp(InNewEvent), FramesUntilExec));
		}
	}

	bool FQuartzClock::CancelQuantizedCommand(TSharedPtr<IQuartzQuantizedCommand> InCommandPtr)
	{
		if (InCommandPtr->IsClockAltering())
		{
			return CancelQuantizedCommandInternal(InCommandPtr, ClockAlteringPendingCommands);
		}

		return CancelQuantizedCommandInternal(InCommandPtr, PendingCommands);
	}

	bool FQuartzClock::HasPendingEvents()
	{
		// if container has any events in it.
		return ((PendingCommands.Num() + ClockAlteringPendingCommands.Num() ) > 0);
	}

	bool FQuartzClock::IsRunning()
	{
		return bIsRunning;
	}

	float FQuartzClock::GetDurationOfQuantizationTypeInSeconds(const EQuartzCommandQuantization& QuantizationType, float Multiplier)
	{
		// if this is unquantized, return 0
		if (QuantizationType == EQuartzCommandQuantization::None)
		{
			return 0;
		}

		FQuartzClockTickRate TickRate = Metronome.GetTickRate();

		// get number of frames until the relevant quantization event
		int64 FramesUntilExec = TickRate.GetFramesPerDuration(QuantizationType);

		//Translate frames to seconds
		float SampleRate = TickRate.GetSampleRate();

		if (SampleRate != 0)
		{
			return (FramesUntilExec * Multiplier) / SampleRate;
		}
		else //Handle potential divide by zero
		{
			return INDEX_NONE;
		}
	}

	FQuartzTransportTimeStamp FQuartzClock::GetCurrentTimestamp()
	{
		FQuartzTransportTimeStamp CurrentTimeStamp = Metronome.GetTimeStamp();

		return CurrentTimeStamp;
	}

	float FQuartzClock::GetEstimatedRunTime()
	{
		return Metronome.GetTimeSinceStart();
	}

	FMixerDevice* FQuartzClock::GetMixerDevice()
	{
		checkSlow(OwningClockManagerPtr);
		if (OwningClockManagerPtr)
		{
			return OwningClockManagerPtr->GetMixerDevice();
		}

		return nullptr;
	}

	FMixerSourceManager* FQuartzClock::GetSourceManager()
	{
		FMixerDevice* MixerDevice = GetMixerDevice();

		checkSlow(MixerDevice);
		if (MixerDevice)
		{
			return MixerDevice->GetSourceManager();
		}
		
		return nullptr;
	}

	FQuartzClockManager* FQuartzClock::GetClockManager()
	{
		checkSlow(OwningClockManagerPtr);
		if (OwningClockManagerPtr)
		{
			return OwningClockManagerPtr;
		}
		return nullptr;
	}

	void FQuartzClock::ResetTransport()
	{
		Metronome.ResetTransport();
	}

	bool FQuartzClock::CancelQuantizedCommandInternal(TSharedPtr<IQuartzQuantizedCommand> InCommandPtr, TArray<PendingCommand>& CommandsToTick)
	{
		for (int32 i = CommandsToTick.Num() - 1; i >= 0; --i)
		{
			PendingCommand& PendingCommand = CommandsToTick[i];

			if (PendingCommand.Command == InCommandPtr)
			{
				PendingCommand.Command->Cancel();
				CommandsToTick.RemoveAtSwap(i);
				return true;
			}
		}

		return false;
	}
} // namespace Audio
