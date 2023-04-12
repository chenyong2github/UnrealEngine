// Copyright Epic Games, Inc. All Rights Reserved.

#include "PresetSettings.h"

#include "UObject/ObjectPtr.h"

TObjectPtr<UPresetUserSettings> UPresetUserSettings::Instance = nullptr;

void UPresetUserSettings::Initialize()
{
	if (Instance == nullptr)
	{
		Instance = NewObject<UPresetUserSettings>();
		Instance->AddToRoot();
	}
}

UPresetUserSettings* UPresetUserSettings::Get()
{
	return Instance;
}
