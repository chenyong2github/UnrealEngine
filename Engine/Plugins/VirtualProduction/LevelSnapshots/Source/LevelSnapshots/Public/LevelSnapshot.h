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

private:
	friend class ULevelSnapshotsFunctionLibrary;

	bool DoesActorHaveSupportedClass(const AActor* Actor);
	void SnapshotActor(AActor* TargetActor);

public:
	// Map of Actor Snapshots mapping from the object path to the actual snapshot
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	TMap<FString, FLevelSnapshot_Actor> ActorSnapshots;

	UPROPERTY(VisibleAnywhere, AssetRegistrySearchable, Category = "Snapshot")
	FSoftObjectPath MapPath;
};
