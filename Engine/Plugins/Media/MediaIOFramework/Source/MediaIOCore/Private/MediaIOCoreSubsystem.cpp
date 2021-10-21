// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaIOCoreSubsystem.h"

TSharedPtr<FMediaIOAudioOutput> UMediaIOCoreSubsystem::CreateAudioOutput(const FCreateAudioOutputArgs& Args)
{
	if (!MediaIOCapture)
	{
		MediaIOCapture = MakeUnique<FMediaIOAudioCapture>();
	}

	return MediaIOCapture->CreateAudioOutput(Args.NumOutputChannels, Args.TargetFrameRate, Args.MaxSampleLatency, Args.OutputSampleRate);
}
