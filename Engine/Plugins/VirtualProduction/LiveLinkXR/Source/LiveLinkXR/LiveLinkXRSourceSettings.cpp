// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkXRSourceSettings.h"


ULiveLinkXRSourceSettings::ULiveLinkXRSourceSettings()
{
	Mode = ELiveLinkSourceMode::EngineTime;
	BufferSettings.EngineTimeOffset = 0.2f;
}
