// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ActorSnapshot.h"
#include "CoreMinimal.h"

#include "LevelSnapshot.generated.h"

UCLASS(BlueprintType, hidecategories = (Object)) // Prevents standard UObject members from showing up in asset editor for this asset
class LEVELSNAPSHOTS_API ULevelSnapshot : public UObject
{
	GENERATED_BODY()

public:

	/**
	 * @brief Creates a snapshot of the target UWorld, polling all Actors
	 * @param TargetWorld 
	 */
	void SnapshotWorld(UWorld* TargetWorld);

private:
	friend class ULevelSnapshotsFunctionLibrary;

	/**
	 * @brief Creates a snapshot of a specific actor
	 * @param TargetActor 
	 */
	void SnapshotActor(AActor* TargetActor);

public:
	// Map of Actor Snapshots mapping from the object path to the actual snapshot
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	TMap<FString, FActorSnapshot> ActorSnapshots;

	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	FString MapName;
};