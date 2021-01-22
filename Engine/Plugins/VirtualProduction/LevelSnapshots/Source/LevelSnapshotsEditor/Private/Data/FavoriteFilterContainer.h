// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "FavoriteFilterContainer.generated.h"

class ULevelSnapshotFilter;
class ULevelSnapshotBlueprintFilter;

/* Keeps track of selected favorite filters. */
UCLASS()
class UFavoriteFilterContainer : public UObject
{
	GENERATED_BODY()
public:

	void AddToFavorites(const TSubclassOf<ULevelSnapshotFilter>& NewFavoriteClass);
	void RemoveFromFavorites(const TSubclassOf<ULevelSnapshotFilter>& NoLongerFavoriteClass);
	void ClearFavorites();

	void SetIncludeAllNativeClasses(bool bShouldIncludeNative);
	void SetIncludeAllBlueprintClasses(bool bShouldIncludeBlueprint);
	bool ShouldIncludeAllNativeClasses() const;
	bool ShouldIncludeAllBlueprintClasses() const;

	// TODO: make getters return classes in alphabetic order of their name
	
	const TArray<TSubclassOf<ULevelSnapshotFilter>>& GetFavorites() const;
	/* Gets filters with the CommonSnapshotFilter uclass meta tag. */
	const TArray<TSubclassOf<ULevelSnapshotFilter>> GetCommonFilters() const;
	/* Gets C++ filters without CommonSnapshotFilter tag. */
	const TArray<TSubclassOf<ULevelSnapshotFilter>>& GetAvailableNativeFilters() const;
	/* Gets Blueprint filters without CommonSnapshotFilter tag; they cannot have this tag btw. */
	TArray<TSubclassOf<ULevelSnapshotBlueprintFilter>> GetAvailableBlueprintFilters() const;
	
	DECLARE_EVENT(UFavoriteFilterContainer, FOnFavoritesModified);
	FOnFavoritesModified OnFavoritesChanged;
	
private:

	// TODO: detect when blueprint class is deleted and remove it from Favorites and LoadedBlueprintClasses
	
	/* The filters the user selected to use. */
	TArray<TSubclassOf<ULevelSnapshotFilter>> Favorites;
	bool bIncludeAllNativeClasses = false;
	bool bIncludeAllBlueprintClasses = false;
	
};