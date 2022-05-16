// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionEditorPerProjectUserSettings.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartition.h"
#include "GameFramework/WorldSettings.h"

#if WITH_EDITOR

void UWorldPartitionEditorPerProjectUserSettings::SetWorldDataLayersNonDefaultEditorLoadStates(UWorld* InWorld, const TArray<FName>& InDataLayersLoadedInEditor, const TArray<FName>& InDataLayersNotLoadedInEditor)
{
	if (ShouldSaveSettings(InWorld))
	{
		FWorldPartitionPerWorldSettings& PerWorldSettings = PerWorldEditorSettings.FindOrAdd(TSoftObjectPtr<UWorld>(InWorld));
		PerWorldSettings.NotLoadedDataLayers = InDataLayersNotLoadedInEditor;
		PerWorldSettings.LoadedDataLayers = InDataLayersLoadedInEditor;
		
		SaveConfig();
	}
}

void UWorldPartitionEditorPerProjectUserSettings::SetEditorGridLoadedRegions(UWorld* InWorld, const TArray<FBox>& InEditorGridLoadedRegions)
{
	if (ShouldSaveSettings(InWorld))
	{
		FWorldPartitionPerWorldSettings& PerWorldSettings = PerWorldEditorSettings.FindOrAdd(TSoftObjectPtr<UWorld>(InWorld));
		PerWorldSettings.LoadedEditorGridRegions = InEditorGridLoadedRegions;
		
		SaveConfig();
	}
}

TArray<FBox> UWorldPartitionEditorPerProjectUserSettings::GetEditorGridLoadedRegions(UWorld* InWorld) const
{
	if (const FWorldPartitionPerWorldSettings* PerWorldSettings = GetWorldPartitionPerWorldSettings(InWorld))
	{
		return PerWorldSettings->LoadedEditorGridRegions;
	}

	return TArray<FBox>();
}

TArray<FName> UWorldPartitionEditorPerProjectUserSettings::GetWorldDataLayersNotLoadedInEditor(UWorld* InWorld) const
{
	if (const FWorldPartitionPerWorldSettings* PerWorldSettings = GetWorldPartitionPerWorldSettings(InWorld))
	{
		return PerWorldSettings->NotLoadedDataLayers;
	}

	return TArray<FName>();
}

TArray<FName> UWorldPartitionEditorPerProjectUserSettings::GetWorldDataLayersLoadedInEditor(UWorld* InWorld) const
{
	if (const FWorldPartitionPerWorldSettings* PerWorldSettings = GetWorldPartitionPerWorldSettings(InWorld))
	{
		return PerWorldSettings->LoadedDataLayers;
	}
	
	return TArray<FName>();
}

const FWorldPartitionPerWorldSettings* UWorldPartitionEditorPerProjectUserSettings::GetWorldPartitionPerWorldSettings(UWorld* InWorld) const
{
	if (const FWorldPartitionPerWorldSettings* ExistingPerWorldSettings = PerWorldEditorSettings.Find(TSoftObjectPtr<UWorld>(InWorld)))
	{
		return ExistingPerWorldSettings;
	}
	else if (const FWorldPartitionPerWorldSettings* DefaultPerWorldSettings = InWorld->GetWorldSettings()->GetDefaultWorldPartitionSettings())
	{
		return DefaultPerWorldSettings;
	}

	return nullptr;
}

#endif