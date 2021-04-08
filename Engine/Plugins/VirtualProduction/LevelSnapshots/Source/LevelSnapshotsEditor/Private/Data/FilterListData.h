// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GameFramework/Actor.h"
#include "PropertySelectionMap.h"
#include "UObject/StrongObjectPtr.h"

#include "FilterListData.generated.h"

class ULevelSnapshot;
class ULevelSnapshotFilter;

/* Holds data for tracking unmodified actor for when "ShowUnchanged = true". */
USTRUCT()
struct FUnmodifiedActors
{
	GENERATED_BODY()
public:
	
	FUnmodifiedActors() = default; // Required by Unreal
	explicit FUnmodifiedActors(const TSet<TWeakObjectPtr<AActor>>& UnmodifiedActors);

	/* Checks whether we need to apply a filter to build inclusion and exclusion set. */
	bool NeedsToApplyFilter() const;
	/* Puts unmodified actors into an inclusion and exclusion set by calling IsActorValid on them. */
	void ApplyFilterToBuildInclusionSet(ULevelSnapshotFilter* FilterToApply);

	TSet<TWeakObjectPtr<AActor>> GetIncludedByFilter() const;
	TSet<TWeakObjectPtr<AActor>> GetExcludedByFilter() const;
	TSet<TWeakObjectPtr<AActor>> GetUnmodifiedActors() const;

private:
	
	/* Initially empty. The actors which passed the filter in ApplyFilterToBuildInclusionSet.
	 * These are the actors to show in filter results panel when "ShowUnchanged = true".
	 */
	TSet<TWeakObjectPtr<AActor>> IncludedByFilter;
	/* Initially empty. The actors which did not pass the filter in ApplyFilterToBuildInclusionSet. */
	TSet<TWeakObjectPtr<AActor>> ExcludedByFilter;

	/* Holds all actors which same serialized data in the selected saved snapshot and editor world. */
	TSet<TWeakObjectPtr<AActor>> UnmodifiedActors;
	
};

/* Contains all data required to display the filter results panel. */
USTRUCT()
struct FFilterListData
{
	GENERATED_BODY()
public:

	FFilterListData() = default; // Required by Unreal 
	FFilterListData(
		ULevelSnapshot* RelatedSnapshot,
		const TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<AActor>>& ModifiedWorldActorToDeserializedSnapshotActor,
		const TSet<TWeakObjectPtr<AActor>>& ModifiedActors,
		const TSet<TWeakObjectPtr<AActor>>& UnmodifiedActors
		);
	
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
	const FPropertySelectionMap& GetUnmodifiedActorsSelectedProperties() const;
	const TSet<TWeakObjectPtr<AActor>>& GetModifiedFilteredActors() const;
	const FUnmodifiedActors& GetUnmodifiedUnfilteredActors() const;

private:

	UPROPERTY()
	ULevelSnapshot* RelatedSnapshot;
	
	/* Initially empty. Contains the selected properties for actors whose serialized data differs in the selected saved snapshot and the editor world. */
	UPROPERTY()
	FPropertySelectionMap ModifiedActorsSelectedProperties;
	/* Initially empty. Contains the selected properties for actors whose serialized data is the same for the selected saved snapshot and the editor world. */
	UPROPERTY()
	FPropertySelectionMap UnmodifiedActorsSelectedProperties;

	/* Only contains actors whose serialized data is not the same as in the selected snapshot. */
	TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<AActor>> ModifiedWorldActorToDeserializedSnapshotActor;

	/* Actors to show in filter results panel when "ShowUnchanged = false". */
	UPROPERTY()
	TSet<TWeakObjectPtr<AActor>> ModifiedFilteredActors;
	/* Actors to show in filter results panel when "ShowUnchanged = true".
	 * You need to call ApplyFilterToBuildInclusionSet first.
	 */
	UPROPERTY()
	FUnmodifiedActors UnmodifiedUnfilteredActors;

};