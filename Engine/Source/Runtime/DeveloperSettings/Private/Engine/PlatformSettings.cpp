// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/PlatformSettings.h"

void FPerPlatformSettings::Initialize(TSubclassOf<UPlatformSettings> SettingsClass)
{
#if WITH_EDITOR
	Settings = UPlatformSettingsManager::Get().GetAllPlatformSettings(SettingsClass);
#endif
}

UPlatformSettings::UPlatformSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}
