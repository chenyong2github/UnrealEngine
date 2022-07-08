// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaSourceManagerSettings.h"

#include "MediaSourceManager.h"

UMediaSourceManager* UMediaSourceManagerSettings::GetManager() const
{
	return Manager.LoadSynchronous();
}

#if WITH_EDITOR

void UMediaSourceManagerSettings::SetManager(UMediaSourceManager* InManager)
{
	Manager = InManager;
	SaveConfig();
	OnManagerChanged.Broadcast();
}

#endif // WITH_EDITOR
