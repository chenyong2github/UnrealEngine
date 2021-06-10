// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GameFramework/Actor.h"
#include "PropertySelectionMap.h"

#include "FilterListData.generated.h"

class AActor;
class ULevelSnapshot;
class ULevelSnapshotFilter;
struct FScopedSlowTask;

/* Contains all data required to display the filter results panel. */
USTRUCT()
struct FFilterListData
{
	GENERATED_BODY()

	void UpdateFilteredList(UWorld* World, ULevelSnapshot* FromSnapshot, ULevelSnapshotFilter* FilterToApply);
	
	/**
	 * Runs IsPropertyValid on all properties of WorldActor.
	 */
	void ApplyFilterToFindSelectedProperties(AActor* WorldActor, ULevelSnapshotFilter* FilterToApply);

	/**
	 * If WorldActor is modified, returns the deserialized actor.
	 * If WorldActor is not modified, returns itself.
	 */
	TWeakObjectPtr<AActor> GetSnapshotCounterpartFor(TWeakObjectPtr<AActor> WorldActor) const;

	const FPropertySelectionMap& GetModifiedActorsSelectedProperties_AllowedByFilter() const { return ModifiedActorsSelectedProperties_AllowedByFilter; }
	const TSet<TWeakObjectPtr<AActor>>& GetModifiedActors_AllowedByFilter() const { return ModifiedWorldActors_AllowedByFilter; }
	const TSet<FSoftObjectPath>& GetRemovedOriginalActorPaths_AllowedByFilter() const { return RemovedOriginalActorPaths_AllowedByFilter; }
	const TSet<TWeakObjectPtr<AActor>>& GetAddedWorldActors_AllowedByFilter() const { return AddedWorldActors_AllowedByFilter; }

	const FPropertySelectionMap& GetModifiedActorsSelectedProperties_DisallowedByFilter() const { return ModifiedActorsSelectedProperties_DisallowedByFilter; }
	const TSet<TWeakObjectPtr<AActor>>& GetModifiedActors_DisallowedByFilter() const { return ModifiedWorldActors_DisallowedByFilter; }
	const TSet<FSoftObjectPath>& GetRemovedOriginalActorPaths_DisallowedByFilter() const { return RemovedOriginalActorPaths_DisallowedByFilter; }
	const TSet<TWeakObjectPtr<AActor>>& GetAddedWorldActors_DisallowedByFilter() const { return AddedWorldActors_DisallowedByFilter; }

private:

	void HandleActorExistsInWorldAndSnapshot(const FSoftObjectPath& OriginalActorPath, ULevelSnapshotFilter* FilterToApply, FScopedSlowTask* Progress);
	void HandleActorWasRemovedFromWorld(const FSoftObjectPath& OriginalActorPath, ULevelSnapshotFilter* FilterToApply);
	void HandleActorWasAddedToWorld(AActor* WorldActor, ULevelSnapshotFilter* FilterToApply);
	
	
	UPROPERTY()
	ULevelSnapshot* RelatedSnapshot = nullptr;

	
	/* Selected properties for actors allowed by filters. */
	UPROPERTY()
	FPropertySelectionMap ModifiedActorsSelectedProperties_AllowedByFilter;

	/* Actors to show in filter results panel when "ShowUnchanged = false". */
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> ModifiedWorldActors_AllowedByFilter;
	/* Actors which existed in snapshot but not in the world. Only contains entries that passed filters. */
	UPROPERTY()
	TSet<FSoftObjectPath> RemovedOriginalActorPaths_AllowedByFilter;
	/* Actors which existed in the world but not in the snapshot. Only contains entries that passed filters. */
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> AddedWorldActors_AllowedByFilter;


	
	/* Selected properties for actors disallowed by filters. */
	UPROPERTY()
	FPropertySelectionMap ModifiedActorsSelectedProperties_DisallowedByFilter;

	/* Actors to show in filter results panel when "ShowUnchanged = true". */
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> ModifiedWorldActors_DisallowedByFilter;
	/* Actors which existed in snapshot but not in the world. Only contains entries that did not pass filters. */
	UPROPERTY()
	TSet<FSoftObjectPath> RemovedOriginalActorPaths_DisallowedByFilter;
	/* Actors which existed in the world but not in the snapshot. Only contains entries that did not pass filters. */
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> AddedWorldActors_DisallowedByFilter;
};