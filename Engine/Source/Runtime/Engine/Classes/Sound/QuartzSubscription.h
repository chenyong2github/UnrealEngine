// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sound/QuartzQuantizationUtilities.h"
#include "UObject/ObjectMacros.h"
#include "Containers/Queue.h"

// forwards
class UQuartzClockHandle;

namespace Audio
{
	// Struct used to communicate command state back to the game play thread
	struct ENGINE_API FQuartzQuantizedCommandDelegateData : public FQuartzCrossThreadMessage
	{
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


	// Class that can be shared between Game Thread and any other thread to queue commands
	// (ONLY the Game Thread may EXECUTE the commands in the queue, enforced by UQuartzClockHandle* argument in the TFunctions)
	class ENGINE_API FShareableQuartzCommandQueue
	{
	public:
		// (add more PushEvent overloads for other event types)
		void PushEvent(const FQuartzQuantizedCommandDelegateData& Data);
		void PushEvent(const FQuartzMetronomeDelegateData& Data);
		bool IsQueueEmpty()
		{
			return EventDelegateQueue.IsEmpty();
		}


		// called when the game object owner is shutting down
		void StopTakingCommands();

	private:
		TQueue<TFunction<void(UQuartzClockHandle*)>, EQueueMode::Mpsc> EventDelegateQueue;
		FThreadSafeBool bIsAcceptingCommands{ true };

		friend class ::UQuartzClockHandle; // UClockHandle is the only class allowed to pump the command queue
	};
} // namespace Audio
