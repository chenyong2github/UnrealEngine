// Copyright Epic Games, Inc. All Rights Reserved.

#include "IAudioLink.h"

#include "EngineAnalytics.h"

IAudioLink::IAudioLink()
{
	if (FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Audio.Usage.AudioLink.InstanceCreated"));
	}
}

