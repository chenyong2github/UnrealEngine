// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsEditorData.h"

#include "FavoriteFilterContainer.h"
#include "DisjunctiveNormalFormFilter.h"
#include "FilterLoader.h"

ULevelSnapshotsEditorData::ULevelSnapshotsEditorData(const FObjectInitializer& ObjectInitializer)
{
	FavoriteFilters = ObjectInitializer.CreateDefaultSubobject<UFavoriteFilterContainer>(
		this,
		TEXT("FavoriteFilters")
		);
	UserDefinedFilters = ObjectInitializer.CreateDefaultSubobject<UDisjunctiveNormalFormFilter>(
		this,
		TEXT("UserDefinedFilters")
		);
	
	FilterLoader = ObjectInitializer.CreateDefaultSubobject<UFilterLoader>(
		this,
		TEXT("FilterLoader")
		);
	FilterLoader->SetAssetBeingEdited(UserDefinedFilters);
	FilterLoader->OnUserSelectedLoadedFilters.AddLambda([this](UDisjunctiveNormalFormFilter* NewFilterToEdit)
	{
		UserDefinedFilters = NewFilterToEdit;
		FilterLoader->SetAssetBeingEdited(UserDefinedFilters);
		OnUserDefinedFiltersChanged.Broadcast();
	});
}

UFavoriteFilterContainer* ULevelSnapshotsEditorData::GetFavoriteFilters() const
{
	return FavoriteFilters;
}

UDisjunctiveNormalFormFilter* ULevelSnapshotsEditorData::GetUserDefinedFilters() const
{
	return UserDefinedFilters;
}

UFilterLoader* ULevelSnapshotsEditorData::GetFilterLoader() const
{
	return FilterLoader;
}
