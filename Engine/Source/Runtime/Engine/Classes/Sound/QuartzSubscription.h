// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sound/QuartzQuantizationUtilities.h"
#include "UObject/ObjectMacros.h"
#include "Containers/Queue.h"
#include "DSP/VolumeFader.h"

// forwards
class FQuartzTickableObject;
class UQuartzClockHandle;

namespace Audio
{
	// Struct used to communicate command state back to the game play thread
	struct ENGINE_API FQuartzQuantizedCommandDelegateData : public FQuartzCrossThreadMessage
	{
		EQuartzCommandType CommandType;
		EQuartzCommandDelegateSubType DelegateSubType;

		// ID so the clock handle knows which delegate to fire
		int32 DelegateID{ -1 };

		// todo: more payload
	};

	// Struct used to communicate metronome events back to the game play thread
	struct ENGINE_API FQuartzMetronomeDelegateData : public FQuartzCrossThreadMessage
	{
		int32 Bar;
		int32 Beat;
		float BeatFraction;
		EQuartzCommandQuantization Quantization;
	};

	//Struct used to queue events to be sent to the Audio Render thread closer to their start time
	struct ENGINE_API FQuartzQueueCommandData : public FQuartzCrossThreadMessage
	{
		FAudioComponentCommandInfo AudioComponentCommandInfo;
		FName ClockName;

		FQuartzQueueCommandData(const FAudioComponentCommandInfo& InAudioComponentCommandInfo, FName InClockName);
	};


	// Class that can be shared between Game Thread and any other thread to queue commands
	// (ONLY the Game Thread may EXECUTE the commands in the queue, enforced by UQuartzClockHandle* argument in the TFunctions)
	class ENGINE_API FShareableQuartzCommandQueue
	{
	public:
		// (add more PushEvent overloads for other event types)
		void PushEvent(const FQuartzQuantizedCommandDelegateData& Data);
		void PushEvent(const FQuartzMetronomeDelegateData& Data);
		void PushEvent(const FQuartzQueueCommandData& Data);

		bool IsQueueEmpty()
		{
			return EventDelegateQueue.IsEmpty();
		}


		// called when the game object owner is shutting down
		void StopTakingCommands();

	private:
		TQueue<TFunction<void(FQuartzTickableObject*)>, EQueueMode::Mpsc> EventDelegateQueue;
		FThreadSafeBool bIsAcceptingCommands{ true };

		friend class ::FQuartzTickableObject; // UQuartzTickableObject, and those that inherit from it, are the only classes allowed to pump the command queue
	};
} // namespace Audio
