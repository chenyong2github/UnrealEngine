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

	void Reset()
	{
		LoadedEditorGridCells.Empty();
		NotLoadedDataLayers.Empty();
		LoadedDataLayers.Empty();
		EditorGridConfigHash = 0;
	}
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

UCLASS(config = EditorPerProjectUserSettings)
class ENGINE_API UWorldPartitionEditorPerProjectUserSettings : public UObject
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
		, bShowOnlySelectedActors(false)
		, bHighlightSelectedDataLayers(true)
		, bDisableLoadingOfLastLoadedCells(false)
#endif
	{}

#if WITH_EDITOR
	void UpdateEditorGridConfigHash(UWorld* InWorld);
	static void UpdateEditorGridConfigHash(UWorld* InWorld, FWorldPartitionPerWorldSettings& PerWorldSettings);
	TArray<FName> GetEditorGridLoadedCells(UWorld* InWorld) const;
	void SetEditorGridLoadedCells(UWorld* InWorld, const TArray<FName>& InEditorGridLoadedCells);

	bool GetEnableLoadingOfLastLoadedCells() const
	{
		return !bDisableLoadingOfLastLoadedCells;
	}

	bool GetBugItGoLoadCells() const
	{
		return bBugItGoLoadCells;
	}

	void SetBugItGoLoadCells(bool bInBugItGoLoadCells)
	{
		if (bBugItGoLoadCells != bInBugItGoLoadCells)
		{
			bBugItGoLoadCells = bInBugItGoLoadCells;
			SaveConfig();
		}
	}

	bool GetShowCellCoords() const
	{
		return bShowCellCoords;
	}

	void SetShowCellCoords(bool bInShowCellCoords)
	{
		if (bShowCellCoords != bInShowCellCoords)
		{
			bShowCellCoords = bInShowCellCoords;
			SaveConfig();
		}
	}

	TArray<FName> GetWorldDataLayersNotLoadedInEditor(UWorld* InWorld) const;
	TArray<FName> GetWorldDataLayersLoadedInEditor(UWorld* InWorld) const;
	
	void SetWorldDataLayersNonDefaultEditorLoadStates(UWorld* InWorld, const TArray<FName>& InDataLayersLoadedInEditor, const TArray<FName>& InDataLayersNotLoadedInEditor);

private:
	const FWorldPartitionPerWorldSettings* GetWorldPartitionPerWorldSettings(UWorld* InWorld) const;
#endif

#if WITH_EDITORONLY_DATA
public:
	/** True when the Data Layer Outliner is displaying Editor Data Layers */
	UPROPERTY(config)
	uint32 bHideEditorDataLayers : 1;

	/** True when the Data Layer Outliner is displaying Runtime Data Layers */
	UPROPERTY(config)
	uint32 bHideRuntimeDataLayers : 1;

	/** True when the Data Layer Outliner is not displaying actors */
	UPROPERTY(config)
	uint32 bHideDataLayerActors : 1;

	/** True when the Data Layer Outliner is not displaying unloaded actors */
	UPROPERTY(config)
	uint32 bHideUnloadedActors : 1;

	/** True when the Data Layer Outliner is only displaying actors and datalayers for selected actors */
	UPROPERTY(config)
	uint32 bShowOnlySelectedActors : 1;

	/** True when the Data Layer Outliner highlights Data Layers containing actors that are currently selected */
	UPROPERTY(config)
	uint32 bHighlightSelectedDataLayers : 1;

private:
	bool ShouldSaveSettings(const UWorld* InWorld) const
	{
		return InWorld && !InWorld->IsGameWorld() && FPackageName::DoesPackageExist(InWorld->GetPackage()->GetName());
	}

	UPROPERTY(config)
	uint32 bDisableLoadingOfLastLoadedCells : 1;

	UPROPERTY(config)
	uint32 bBugItGoLoadCells : 1;

	UPROPERTY(config)
	uint32 bShowCellCoords : 1;

	UPROPERTY(config)
	TMap<TSoftObjectPtr<UWorld>, FWorldPartitionPerWorldSettings> PerWorldEditorSettings;
#endif
};