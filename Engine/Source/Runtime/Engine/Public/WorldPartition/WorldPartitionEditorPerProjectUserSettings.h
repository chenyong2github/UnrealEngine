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

	UPROPERTY()
	TArray<FName> LoadedDataLayers;

	UPROPERTY()
	uint32 EditorGridConfigHash = 0;
#endif
};

UCLASS(MinimalAPI, config = EditorPerProjectUserSettings)
class UWorldPartitionEditorPerProjectUserSettings : public UObject
{
	GENERATED_BODY()

public:
	UWorldPartitionEditorPerProjectUserSettings(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
		, bHideEditorDataLayers(false)
		, bHideRuntimeDataLayers(false)
		, bHideDataLayerActors(true)
		, bHideUnloadedActors(false)
		, bAllowRuntimeDataLayerEditing(false)
		, bDisableLoadingOfLastLoadedCells(false)
#endif
	{}

#if WITH_EDITOR
	void SetEditorGridConfigHash(UWorld* InWorld, uint32 InEditorGridConfigHash)
	{ 
		if (ShouldSaveSettings(InWorld))
		{
			FWorldPartitionPerWorldSettings& PerWorldSettings = PerWorldEditorSettings.FindOrAdd(TSoftObjectPtr<UWorld>(InWorld));
			if (PerWorldSettings.EditorGridConfigHash != InEditorGridConfigHash)
			{
				PerWorldSettings.EditorGridConfigHash = InEditorGridConfigHash;
				PerWorldSettings.LoadedEditorGridCells.Empty();
				SaveConfig();
			}
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

	bool GetEnableLoadingOfLastLoadedCells() const
	{
		return !bDisableLoadingOfLastLoadedCells;
	}

	const TArray<FName>& GetWorldDataLayersNotLoadedInEditor(UWorld* InWorld)
	{
		return PerWorldEditorSettings.FindOrAdd(TSoftObjectPtr<UWorld>(InWorld)).NotLoadedDataLayers;
	}

	const TArray<FName>& GetWorldDataLayersLoadedInEditor(UWorld* InWorld)
	{
		return PerWorldEditorSettings.FindOrAdd(TSoftObjectPtr<UWorld>(InWorld)).LoadedDataLayers;
	}

	void SetWorldDataLayersNonDefaultEditorLoadStates(UWorld* InWorld, const TArray<FName>& InDataLayersLoadedInEditor, const TArray<FName>& InDataLayersNotLoadedInEditor)
	{
		if (ShouldSaveSettings(InWorld))
		{
			FWorldPartitionPerWorldSettings& PerWorldSettings = PerWorldEditorSettings.FindOrAdd(TSoftObjectPtr<UWorld>(InWorld));

			PerWorldSettings.NotLoadedDataLayers = InDataLayersNotLoadedInEditor;
			PerWorldSettings.LoadedDataLayers = InDataLayersLoadedInEditor;
			SaveConfig();
		}
	}
#endif

#if WITH_EDITORONLY_DATA
public:
	/** True when the Data Layer Outliner is displaying Editor Data Layers */
	UPROPERTY(config)
	uint32 bHideEditorDataLayers : 1;

	/** True when the Data Layer Outliner is displaying Runtime Data Layers */
	UPROPERTY(config)
	uint32 bHideRuntimeDataLayers : 1;

	/** True when the DataLayer Outliner is not displaying actors */
	UPROPERTY(config)
	uint32 bHideDataLayerActors : 1;

	/** True when the DataLayer Outliner is not displaying unloaded actors */
	UPROPERTY(config)
	uint32 bHideUnloadedActors : 1;

	/** True when Runtime DataLayer editing is allowed. */
	UPROPERTY(config)
	uint32 bAllowRuntimeDataLayerEditing : 1;

private:
	bool ShouldSaveSettings(const UWorld* InWorld) const
	{
		return InWorld && !InWorld->IsGameWorld() && FPackageName::DoesPackageExist(InWorld->GetPackage()->GetName());
	}

	UPROPERTY(config)
	uint32 bDisableLoadingOfLastLoadedCells : 1;

	UPROPERTY(config)
	TMap<TSoftObjectPtr<UWorld>, FWorldPartitionPerWorldSettings> PerWorldEditorSettings;
#endif
};