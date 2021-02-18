// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Sound/QuartzQuantizationUtilities.h"
#include "Quartz/QuartzMetronome.h"

namespace Audio
{
	// forwards
	class FMixerDevice;
	class FMixerSourceManager;
	class FQuartzClockManager;

	// Contains the pending command and the number of frames it has to wait to fire
	struct PendingCommand
	{
		// ctor
		PendingCommand(TSharedPtr<IQuartzQuantizedCommand> InCommand, int32 InNumFramesUntilExec)
		: Command(InCommand)
		, NumFramesUntilExec(InNumFramesUntilExec)
		{
		}

		// Quantized Command Object
		TSharedPtr<IQuartzQuantizedCommand> Command;

		// Countdown to execution
		int32 NumFramesUntilExec{ 0 };

		// TODO: int32 TotalNumFramesThreadLatency{ 0 };

	}; // struct PendingCommand

	// Class that encapsulates sample-accurate timing logic, as well as firing QuantizedAudioCommands
	class AUDIOMIXER_API FQuartzClock
	{
	public:

		// ctor
		FQuartzClock(const FName& InName, const FQuartzClockSettings& InClockSettings, FQuartzClockManager* InOwningClockManagerPtr = nullptr);

		// dtor
		~FQuartzClock();

		// alter the tick rate (take by-value to make sample-rate adjustments in-place)
		void ChangeTickRate(FQuartzClockTickRate InNewTickRate, int32 NumFramesLeft = 0);

		// alter the time signature
		void ChangeTimeSignature(const FQuartzTimeSignature& InNewTimeSignature);

		// start ticking the clock
		void Resume();

		// stop ticking and reset the clock
		void Stop(bool CancelPendingEvents);

		// stop ticking the clock
		void Pause();

		// reset the metronome
		void Restart(bool bPause = true);

		// shutdown
		void Shutdown();

		// tick the clock
		void Tick(int32 InNumFramesUntilNextTick);

		// Set the sample rate of the clock
		void SetSampleRate(float InNewSampleRate);

		// get the tick rate
		FQuartzClockTickRate GetTickRate() { return Metronome.GetTickRate(); }

		// get the identifier of the clock
		FName GetName() const { return Name; }

		// clock will persist across level changes
		bool IgnoresFlush();

		// Does this clock match the provided settings
		bool DoesMatchSettings(const FQuartzClockSettings& InClockSettings) const;

		void SubscribeToTimeDivision(MetronomeCommandQueuePtr InListenerQueue, EQuartzCommandQuantization InQuantizationBoundary);

		void SubscribeToAllTimeDivisions(MetronomeCommandQueuePtr InListenerQueue);

		void UnsubscribeFromTimeDivision(MetronomeCommandQueuePtr InListenerQueue, EQuartzCommandQuantization InQuantizationBoundary);

		void UnsubscribeFromAllTimeDivisions(MetronomeCommandQueuePtr InListenerQueue);

		// Add a new event to be triggered by this clock
		// TODO: return a handle to this event so "looping" events can be canceled
		void AddQuantizedCommand(FQuartzQuantizationBoundary InQuantizationBondary, TSharedPtr<IQuartzQuantizedCommand> InNewEvent);

		// Cancel pending command
		bool CancelQuantizedCommand(TSharedPtr<IQuartzQuantizedCommand> InCommandPtr);

		// does the clock have any pending events
		bool HasPendingEvents();

		// is the clock currently ticking?
		bool IsRunning();

		FMixerDevice* GetMixerDevice();

		FMixerSourceManager* GetSourceManager();

		FQuartzClockManager* GetClockManager();

		void ResetTransport();

		void AddToTickDelay(int32 NumFramesOfDelayToAdd)
		{
			TickDelayLengthInFrames += NumFramesOfDelayToAdd;
		}

		void SetTickDelay(int32 NumFramesOfDelay)
		{
			TickDelayLengthInFrames = NumFramesOfDelay;
		}

	private:
		void TickInternal(int32 InNumFramesUntilNextTick, TArray<PendingCommand>& CommandsToTick, int32 FramesOfLatency = 0, int32 FramesOfDelay = 0);

		bool CancelQuantizedCommandInternal(TSharedPtr<IQuartzQuantizedCommand> InCommandPtr, TArray<PendingCommand>& CommandsToTick);

		// don't allow default ctor, a clock needs to be ready to be used
		// by the clock manager / FMixerDevice once constructed
		FQuartzClock() = delete;

		FQuartzMetronome Metronome;

		FQuartzClockManager* OwningClockManagerPtr{ nullptr };

		FName Name;

		// TODO: Make this configurable
		float ThreadLatencyInMilliseconds{ 40.f };

		// Container of external commands to be executed (TUniquePointer<QuantizedAudioCommand>)
		TArray<PendingCommand> ClockAlteringPendingCommands;
		TArray<PendingCommand> PendingCommands;

		FThreadSafeBool bIsRunning{ true };

		bool bIgnoresFlush{ false };

		int32 TickDelayLengthInFrames{ 0 };

	}; // class FQuartzClock

} // namespace Audio
