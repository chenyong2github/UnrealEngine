// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"
#include "Engine/World.h"
#include "WorldPartitionEditorPerProjectUserSettings.generated.h"

USTRUCT()
struct FWorldPartitionPerWorldSettings
{
	GENERATED_BODY();

#if WITH_EDITOR
	FWorldPartitionPerWorldSettings()
	{}

	FWorldPartitionPerWorldSettings(TArray<FName>& InLoadedEditorGridCells)
		: LoadedEditorGridCells(InLoadedEditorGridCells)
	{}
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FName> LoadedEditorGridCells;

	UPROPERTY()
	TArray<FName> NotLoadedDataLayers;
#endif
};

UCLASS(MinimalAPI, config = EditorPerProjectUserSettings)
class UWorldPartitionEditorPerProjectUserSettings : public UObject
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	uint32 GetEditorGridConfigHash() const
	{ 
		return EditorGridConfigHash;
	}

	void SetEditorGridConfigHash(uint32 InEditorGridConfigHash)
	{ 
		if (EditorGridConfigHash != InEditorGridConfigHash)
		{
			EditorGridConfigHash = InEditorGridConfigHash;

			for (auto& PerWorldEditorSetting : PerWorldEditorSettings)
			{
				PerWorldEditorSetting.Value.LoadedEditorGridCells.Empty();
			}
			SaveConfig();
		}
	}

	const TArray<FName>& GetEditorGridLoadedCells(UWorld* InWorld)
	{ 
		return PerWorldEditorSettings.FindOrAdd(TSoftObjectPtr<UWorld>(InWorld)).LoadedEditorGridCells;
	}

	void SetEditorGridLoadedCells(UWorld* InWorld, TArray<FName>& InEditorGridLoadedCells)
	{
		if (ShouldSaveSettings(InWorld))
		{
			TArray<FName>& GridLoadedCells = PerWorldEditorSettings.FindOrAdd(TSoftObjectPtr<UWorld>(InWorld)).LoadedEditorGridCells;
			GridLoadedCells = InEditorGridLoadedCells;
			SaveConfig();
		}
	}

	bool GetShowDataLayerContent() const
	{
		return bShowDataLayerContent;
	}

	void SetShowDataLayerContent(bool bInShowDataLayerContent)
	{
		if (bShowDataLayerContent != bInShowDataLayerContent)
		{
			bShowDataLayerContent = bInShowDataLayerContent;
			SaveConfig();
		}
	}

	bool GetEnableLoadingOfLastLoadedCells() const
	{
		return !bDisableLoadingOfLastLoadedCells;
	}

	const TArray<FName>& GetWorldDataLayersNotLoadedInEditor(UWorld* InWorld)
	{
		return PerWorldEditorSettings.FindOrAdd(TSoftObjectPtr<UWorld>(InWorld)).NotLoadedDataLayers;
	}

	void SetWorldDataLayersNotLoadedInEditor(UWorld* InWorld, const TArray<FName>& InDataLayersLoadedInEditor)
	{
		if (ShouldSaveSettings(InWorld))
		{
			TArray<FName>& DataLayersNotLoadedInEditor = PerWorldEditorSettings.FindOrAdd(TSoftObjectPtr<UWorld>(InWorld)).NotLoadedDataLayers;
			DataLayersNotLoadedInEditor = InDataLayersLoadedInEditor;
			SaveConfig();
		}
	}
#endif

private:
#if WITH_EDITORONLY_DATA
	bool ShouldSaveSettings(const UWorld* InWorld) const
	{
		return InWorld && !InWorld->IsGameWorld();
	}

	UPROPERTY(config)
	uint32 EditorGridConfigHash;

	UPROPERTY(config)
	uint32 bShowDataLayerContent : 1;

	UPROPERTY(config)
	uint32 bDisableLoadingOfLastLoadedCells : 1;

	UPROPERTY(config)
	TMap<TSoftObjectPtr<UWorld>, FWorldPartitionPerWorldSettings> PerWorldEditorSettings;
#endif
};