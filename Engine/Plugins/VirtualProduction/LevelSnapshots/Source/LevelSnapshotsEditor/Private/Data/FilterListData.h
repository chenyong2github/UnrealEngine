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
	
	/* Runs IsPropertyValid on all properties of WorldActor.
	 * 
	 * Puts the results into ModifiedActorSelectedProperties or UnmodifiedActorSelectedProperties, respectively.
	 * @return A map containing the selected properties for WorldActor: either ModifiedActorSelectedProperties or UnmodifiedActorSelectedProperties.
	 */
	const FPropertySelectionMap& ApplyFilterToFindSelectedProperties(AActor* WorldActor, ULevelSnapshotFilter* FilterToApply);

	/*
	 * If WorldActor is modified, returns the deserialized actor.
	 * If WorldActor is not modified, returns itself.
	 */
	TWeakObjectPtr<AActor> GetSnapshotCounterpartFor(TWeakObjectPtr<AActor> WorldActor) const;

	const FPropertySelectionMap& GetModifiedActorsSelectedProperties() const;
	const TSet<TWeakObjectPtr<AActor>>& GetModifiedFilteredActors() const;
	const TSet<FSoftObjectPath>& GetFilteredRemovedOriginalActorPaths() const;
	const TSet<TWeakObjectPtr<AActor>>& GetFilteredAddedWorldActors() const;

private:

	void HandleActorExistsInWorldAndSnapshot(const FSoftObjectPath& OriginalActorPath, ULevelSnapshotFilter* FilterToApply, FScopedSlowTask* Progress);
	void HandleActorWasRemovedFromWorld(const FSoftObjectPath& OriginalActorPath, ULevelSnapshotFilter* FilterToApply);
	void HandleActorWasAddedToWorld(AActor* WorldActor, ULevelSnapshotFilter* FilterToApply);
	
	
	UPROPERTY()
	ULevelSnapshot* RelatedSnapshot = nullptr;
	
	/* Initially empty. Contains the selected properties for actors whose serialized data differs in the selected saved snapshot and the editor world. */
	UPROPERTY()
	FPropertySelectionMap ModifiedActorsSelectedProperties;

	/* Actors to show in filter results panel when "ShowUnchanged = false". */
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> ModifiedFilteredActors;

	/* Actors which existed in snapshot but not in the world. Only contains entries that passed filters. */
	UPROPERTY()
	TSet<FSoftObjectPath> FilteredRemovedOriginalActorPaths;

	/* Actors which existed in the world but not in the snapshot. Only contains entries that passed filters. */
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> FilteredAddedWorldActors;

};