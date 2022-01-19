// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modes/PlacementModeSubsystem.h"

#include "AssetPlacementSettings.h"
#include "Subsystems/PlacementSubsystem.h"

#include "Editor.h"

void UPlacementModeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UPlacementSubsystem>();

	GEditor->GetEditorSubsystem<UPlacementSubsystem>()->OnPlacementFactoriesRegistered().AddUObject(this, &UPlacementModeSubsystem::LoadSettings);
	FCoreDelegates::OnEnginePreExit.AddUObject(this, &UPlacementModeSubsystem::SaveSettings);
}

void UPlacementModeSubsystem::Deinitialize()
{
	GEditor->GetEditorSubsystem<UPlacementSubsystem>()->OnPlacementFactoriesRegistered().RemoveAll(this);
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

void UPlacementModeSubsystem::LoadSettings()
{
	ModeSettings = NewObject<UAssetPlacementSettings>(this);
	ModeSettings->LoadSettings();

	GEditor->GetEditorSubsystem<UPlacementSubsystem>()->OnPlacementFactoriesRegistered().RemoveAll(this);
}
