// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UActorComponent;

struct FActorSnapshotData;
struct FPropertySelectionMap;
struct FWorldSnapshotData;

namespace SnapshotUtil::Component
{
	/** Adds and removes components on an actor that exists in the snapshot world (and existed the snapshot were applied). */
	void AddAndRemoveComponentsSelectedForRestore(AActor* MatchedEditorActor, const FActorSnapshotData& SnapshotData, FWorldSnapshotData& WorldData, const FPropertySelectionMap& SelectionMap, UPackage* LocalisationSnapshotPackage);
	
	/** Recreates all components an actor recreated in the editor world. */
	void AllocateMissingComponentsForRecreatedActor(AActor* RecreatedEditorActor, const FActorSnapshotData& SnapshotData, FWorldSnapshotData& WorldData);

	/** Recreates all components on the actor in the snapshot world. */
	void AllocateMissingComponentsForSnapshotActor(AActor* SnapshotActor, const FActorSnapshotData& SnapshotData, FWorldSnapshotData& WorldData);
}



