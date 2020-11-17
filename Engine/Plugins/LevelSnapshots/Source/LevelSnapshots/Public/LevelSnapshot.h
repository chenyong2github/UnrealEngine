// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ActorSnapshot.h"

#include "LevelSnapshot.generated.h"

class ULevelSnapshotTrackingComponent;

UCLASS(BlueprintType)
class LEVELSNAPSHOTS_API ULevelSnapshot : public UObject
{
	GENERATED_BODY()

public:

	void SnapshotWorld(UWorld* TargetWorld);

	// This method will look for a UniqueID in the deserialized object and if it doesn't have one, it will generate one.
	// Valid UniqueIdentifiers will be added to the TSet 'UniqueIdentifiers' to ensure no two actors have the same ID
	int32 GetOrGenerateUniqueID(ULevelSnapshotTrackingComponent* TrackingComponent);

private:
	friend class ULevelSnapshotsFunctionLibrary;

	void FindOrCreateTrackingComponent(AActor* TargetActor);
	bool DoesActorHaveSupportedClass(const AActor* Actor);
	void SnapshotActor(AActor* TargetActor);

public:
	// Map of Actor Snapshots mapping from the object path to the actual snapshot
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	TMap<FString, FLevelSnapshot_Actor> ActorSnapshots;

	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	FString MapName;

	UPROPERTY(BlueprintReadOnly, Category = "Snapshot")
	TSet<int32> UniqueIdentifiers;
};

/**
 * @brief This component is added to all actors that are serialized in a snapshot to allow us to save information used by the snapshot system
 */
UCLASS(BlueprintType)
class LEVELSNAPSHOTS_API ULevelSnapshotTrackingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULevelSnapshotTrackingComponent();

	// This is an identifier used to match objects in a saved snapshot to objects in the current level
	// Using names to match would require that names and parentage never change in a level which is unrealistic
	UPROPERTY()
	int32 UniqueIdentifier;
};
