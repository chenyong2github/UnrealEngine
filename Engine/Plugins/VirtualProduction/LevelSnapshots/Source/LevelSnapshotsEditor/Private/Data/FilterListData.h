// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GameFramework/Actor.h"
#include "Selection/PropertySelectionMap.h"

class AActor;
class ULevelSnapshot;
class ULevelSnapshotFilter;
struct FScopedSlowTask;

/* Contains all data required to display the filter results panel. */
struct FFilterListData
{
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

	const FPropertySelectionMap& GetModifiedEditorObjectsSelectedProperties_AllowedByFilter() const { return ModifiedEditorObjectsSelectedProperties_AllowedByFilter; }
	const TSet<TWeakObjectPtr<AActor>>& GetModifiedActors_AllowedByFilter() const { return ModifiedWorldActors_AllowedByFilter; }
	const TSet<FSoftObjectPath>& GetRemovedOriginalActorPaths_AllowedByFilter() const { return RemovedOriginalActorPaths_AllowedByFilter; }
	const TSet<TWeakObjectPtr<AActor>>& GetAddedWorldActors_AllowedByFilter() const { return AddedWorldActors_AllowedByFilter; }

	const FPropertySelectionMap& GetModifiedEditorObjectsSelectedProperties_DisallowedByFilter() const { return ModifiedEditorObjectsSelectedProperties_DisallowedByFilter; }
	const TSet<TWeakObjectPtr<AActor>>& GetModifiedActors_DisallowedByFilter() const { return ModifiedWorldActors_DisallowedByFilter; }
	const TSet<FSoftObjectPath>& GetRemovedOriginalActorPaths_DisallowedByFilter() const { return RemovedOriginalActorPaths_DisallowedByFilter; }
	const TSet<TWeakObjectPtr<AActor>>& GetAddedWorldActors_DisallowedByFilter() const { return AddedWorldActors_DisallowedByFilter; }

private:

	void HandleActorExistsInWorldAndSnapshot(const FSoftObjectPath& OriginalActorPath, ULevelSnapshotFilter* FilterToApply, FScopedSlowTask* Progress);
	void HandleActorWasRemovedFromWorld(const FSoftObjectPath& OriginalActorPath, ULevelSnapshotFilter* FilterToApply);
	void HandleActorWasAddedToWorld(AActor* WorldActor, ULevelSnapshotFilter* FilterToApply);
	
	
	TWeakObjectPtr<ULevelSnapshot> RelatedSnapshot = nullptr;

	
	/* Selected properties for actors allowed by filters. */
	FPropertySelectionMap ModifiedEditorObjectsSelectedProperties_AllowedByFilter;

	/* Actors to show in filter results panel when "ShowUnchanged = false". */
	TSet<TWeakObjectPtr<AActor>> ModifiedWorldActors_AllowedByFilter;
	/* Actors which existed in snapshot but not in the world. Only contains entries that passed filters. */
	TSet<FSoftObjectPath> RemovedOriginalActorPaths_AllowedByFilter;
	/* Actors which existed in the world but not in the snapshot. Only contains entries that passed filters. */
	TSet<TWeakObjectPtr<AActor>> AddedWorldActors_AllowedByFilter;


	
	/* Selected properties for actors disallowed by filters. */
	FPropertySelectionMap ModifiedEditorObjectsSelectedProperties_DisallowedByFilter;

	/* Actors to show in filter results panel when "ShowUnchanged = true". */
	TSet<TWeakObjectPtr<AActor>> ModifiedWorldActors_DisallowedByFilter;
	/* Actors which existed in snapshot but not in the world. Only contains entries that did not pass filters. */
	TSet<FSoftObjectPath> RemovedOriginalActorPaths_DisallowedByFilter;
	/* Actors which existed in the world but not in the snapshot. Only contains entries that did not pass filters. */
	TSet<TWeakObjectPtr<AActor>> AddedWorldActors_DisallowedByFilter;
};