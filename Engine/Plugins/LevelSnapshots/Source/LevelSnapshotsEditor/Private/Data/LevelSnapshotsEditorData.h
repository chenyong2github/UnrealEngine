// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "LevelSnapshotsEditorData.generated.h"

class ULevelSnapshotFilter;
class UFavoriteFilterContainer;

UCLASS()
class ULevelSnapshotEditorFilterGroup : public UObject
{
	GENERATED_BODY()

public:
	ULevelSnapshotFilter* AddOrFindFilter(TSubclassOf<ULevelSnapshotFilter> InClass, const FName& InName);

public:
	UPROPERTY()
	TMap<FName, ULevelSnapshotFilter*> Filters;
};

UCLASS()
class LEVELSNAPSHOTSEDITOR_API ULevelSnapshotsEditorData : public UObject
{
	GENERATED_BODY()

public:

	ULevelSnapshotsEditorData(const FObjectInitializer& ObjectInitializer);
	
	ULevelSnapshotEditorFilterGroup* AddOrFindGroup(const FName& InName);

	UFavoriteFilterContainer* GetFavoriteFilters() const;
	
public:

	// TODO: FilterGroups and AddOrFindGroup will be extracted & refactored into a separate class
	
	UPROPERTY()
	TMap<FName, ULevelSnapshotEditorFilterGroup*> FilterGroups;

private:

	UPROPERTY()
	UFavoriteFilterContainer* FavoriteFilters;
	
};