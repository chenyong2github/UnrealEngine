// Copyright Epic Games, Inc. All Rights Reserved.

#include "Quartz/AudioMixerClock.h"
#include "Quartz/AudioMixerClockManager.h"
#include "AudioMixerSourceManager.h"

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
	}

	FQuartzClock::~FQuartzClock()
	{
	}

	void FQuartzClock::ChangeTickRate(FQuartzClockTickRate InNewTickRate, int32 NumFramesLeft)
	{
		FMixerDevice* MixerDevice = GetMixerDevice();

		if (MixerDevice)
		{
			InNewTickRate.SetSampleRate(MixerDevice->GetSampleRate());
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

	void FQuartzClock::Tick(int32 InNumFramesUntilNextTick)
	{
		const int32 FramesOfLatency = (ThreadLatencyInMilliseconds / 1000) * Metronome.GetTickRate().GetSampleRate();

		if (!bIsRunning)
		{
			return;
		}

		TickInternal(InNumFramesUntilNextTick, ClockAlteringPendingCommands, FramesOfLatency); // (process things like BPM changes first)
		TickInternal(InNumFramesUntilNextTick, PendingCommands, FramesOfLatency);

		Metronome.Tick(InNumFramesUntilNextTick, FramesOfLatency);
	}

	void FQuartzClock::TickInternal(int32 InNumFramesUntilNextTick, TArray<PendingCommand>& CommandsToTick, int32 FramesOfLatency)
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
				PendingCommand.Command->OnFinalCallback(PendingCommand.NumFramesUntilExec);
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

	void FQuartzClock::ResetTransport()
	{
		Metronome.ResetTransport();
	}

	bool FQuartzClock::CancelQuantizedCommandInternal(TSharedPtr<IQuartzQuantizedCommand> InCommandPtr, TArray<PendingCommand>& CommandsToTick)
	{
		for (int32 i = PendingCommands.Num() - 1; i >= 0; --i)
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
