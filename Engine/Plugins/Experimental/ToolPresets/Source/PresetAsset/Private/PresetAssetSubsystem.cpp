// Copyright Epic Games, Inc. All Rights Reserved.

#include "PresetAssetSubsystem.h"
#include "PresetAsset.h"
#include "EditorConfigSubsystem.h"

#define LOCTEXT_NAMESPACE "PresetAssetSubsystem"

void UPresetAssetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency(UEditorConfigSubsystem::StaticClass());

	InitializeDefaultCollection();
}

void UPresetAssetSubsystem::Deinitialize()
{
	if (DefaultCollection)
	{
		DefaultCollection->SaveEditorConfig();
		DefaultCollection = nullptr;
	}	
}

UInteractiveToolsPresetCollectionAsset* UPresetAssetSubsystem::GetDefaultCollection()
{
	return DefaultCollection;
}

bool UPresetAssetSubsystem::SaveDefaultCollection()
{
	if (DefaultCollection)
	{
		return DefaultCollection->SaveEditorConfig();
	}
	return false;
}


void UPresetAssetSubsystem::InitializeDefaultCollection()
{
	/*
	* We're storing the default collection as a JSON file instead of an asset
	* on disk for a few reasons. First it avoids issues around automatically
	* creating assets, both from avoiding build system issues and from a more
	* philisophical point about requiring user involvement. Second, it helps
	* compartmentalizing the "default" collection as more of Editor preferences,
	* rather than a specific collection which has purpose and can be shared around. 
	*/

	DefaultCollection = NewObject<UInteractiveToolsPresetCollectionAsset>();
	DefaultCollection->CollectionLabel = LOCTEXT("DefaultCollectionLabel", "Personal Presets (Default)");
	DefaultCollection->LoadEditorConfig();
}

#undef LOCTEXT_NAMESPACE