// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sound/QuartzSubscription.h"

#include "Quartz/AudioMixerClockHandle.h"

namespace Audio
{
	FQuartzQueueCommandData::FQuartzQueueCommandData(const FAudioComponentCommandInfo& InAudioComponentCommandInfo, FName InClockName)
		: AudioComponentCommandInfo(InAudioComponentCommandInfo),
		ClockName(InClockName)
	{}

	void FShareableQuartzCommandQueue::PushEvent(const FQuartzQuantizedCommandDelegateData& Data)
	{
		if (bIsAcceptingCommands)
		{
			EventDelegateQueue.Enqueue([InData = Data](FQuartzTickableObject* Handle) { Handle->ProcessCommand(InData); });
		}
	}

	void FShareableQuartzCommandQueue::PushEvent(const FQuartzMetronomeDelegateData& Data)
	{
		if (bIsAcceptingCommands)
		{
			EventDelegateQueue.Enqueue([InData = Data](FQuartzTickableObject* Handle) { Handle->ProcessCommand(InData); });
		}
	}

	void FShareableQuartzCommandQueue::PushEvent(const FQuartzQueueCommandData& Data)
	{
		if (bIsAcceptingCommands)
		{
			EventDelegateQueue.Enqueue([InData = Data](FQuartzTickableObject* Handle) { Handle->ProcessCommand(InData); });
		}
	}


	void FShareableQuartzCommandQueue::StopTakingCommands()
	{
		bIsAcceptingCommands = false;
		EventDelegateQueue.Empty();
	}

} // namespace Audio