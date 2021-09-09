// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modes/PlacementModeSubsystem.h"

#include "AssetPlacementSettings.h"


#include "Editor.h"

void UPlacementModeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	ModeSettings = NewObject<UAssetPlacementSettings>(this);
	ModeSettings->LoadSettings();

	FCoreDelegates::OnEnginePreExit.AddUObject(this, &UPlacementModeSubsystem::SaveSettings);
}

void UPlacementModeSubsystem::Deinitialize()
{
	ModeSettings = nullptr;
}

const UAssetPlacementSettings* UPlacementModeSubsystem::GetModeSettingsObject() const
{
	return ModeSettings;
}

UAssetPlacementSettings* UPlacementModeSubsystem::GetMutableModeSettingsObject()
{
	return ModeSettings;
}

void UPlacementModeSubsystem::SaveSettings() const
{
	if (ModeSettings)
	{
		ModeSettings->SaveSettings();
	}
}
