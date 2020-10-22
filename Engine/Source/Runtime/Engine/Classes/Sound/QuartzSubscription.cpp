// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sound/QuartzSubscription.h"

#include "Quartz/AudioMixerClockHandle.h"

namespace Audio
{
	void FShareableQuartzCommandQueue::PushEvent(const FQuartzQuantizedCommandDelegateData& Data)
	{
		if (bIsAcceptingCommands)
		{
			EventDelegateQueue.Enqueue([InData = Data](UQuartzClockHandle* Handle) { Handle->ProcessCommand(InData); });
		}
	}

	void FShareableQuartzCommandQueue::PushEvent(const FQuartzMetronomeDelegateData& Data)
	{
		if (bIsAcceptingCommands)
		{
			EventDelegateQueue.Enqueue([InData = Data](UQuartzClockHandle* Handle) { Handle->ProcessCommand(InData); });
		}
	}

	void FShareableQuartzCommandQueue::StopTakingCommands()
	{
		bIsAcceptingCommands = false;
		EventDelegateQueue.Empty();
	}

} // namespace Audio