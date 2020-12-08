// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FilterLoader.generated.h"

class UDisjunctiveNormalFormFilter;

/* Handles saving and loading of UDisjunctiveNormalFormFilter. */
UCLASS()
class UFilterLoader : public UObject
{
	GENERATED_BODY()
public:

	void OverwriteExisting();
	void SaveAs();
	void LoadAsset(const FAssetData& PickedAsset);

	TOptional<FAssetData> GetAssetLastSavedOrLoaded() const;
	
	DECLARE_EVENT_OneParam(UFilterLoader, FOnLoadedFilters, UDisjunctiveNormalFormFilter*);
	/* Called by LoadAsset. Tells everyone to fix up all references. */
	FOnLoadedFilters OnUserSelectedLoadedFilters;

	DECLARE_EVENT(UFilterLoader, FOnFilterWasSavedOrLoaded);
	/* Used by UI to know when SaveLoaded option may be shown. */
	FOnFilterWasSavedOrLoaded OnFilterWasSavedOrLoaded; 

private:

	friend class ULevelSnapshotsEditorData;
	void SetAssetBeingEdited(UDisjunctiveNormalFormFilter* NewAssetBeingEdited);

	void OnSaveOrLoadAssetOnDisk(const FAssetData& AssetOnDisk);
	void SetAssetLastSavedOrLoaded(const FAssetData& NewAsset);
	
	/* Set once user either has used RequestSaveAs or SetPickedAsset. */
	TOptional<FAssetData> AssetLastSavedOrLoaded;

	UDisjunctiveNormalFormFilter* AssetBeingEdited;
	
};
