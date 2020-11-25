// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "LevelSnapshotsEditorData.generated.h"

class UDisjunctiveNormalFormFilter;
class UFavoriteFilterContainer;

/* Stores all data shared across the editor's UI. */
UCLASS()
class LEVELSNAPSHOTSEDITOR_API ULevelSnapshotsEditorData : public UObject
{
	GENERATED_BODY()
public:

	ULevelSnapshotsEditorData(const FObjectInitializer& ObjectInitializer);

	UFavoriteFilterContainer* GetFavoriteFilters() const;
	UDisjunctiveNormalFormFilter* GetUserDefinedFilters() const;

private:

	UPROPERTY()
	UFavoriteFilterContainer* FavoriteFilters;
	/* Stores user-defined filters in chain of ORs of ANDs. */
	UPROPERTY()
	UDisjunctiveNormalFormFilter* UserDefinedFilters;
	
};