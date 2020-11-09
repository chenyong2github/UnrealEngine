// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ActorSnapshot.h"
#include "CoreMinimal.h"

#include "LevelSnapshot.generated.h"

UCLASS(BlueprintType)
class LEVELSNAPSHOTS_API ULevelSnapshot : public UObject
{
	GENERATED_BODY()

public:

	void SnapshotWorld(UWorld* TargetWorld);

private:
	friend class ULevelSnapshotsFunctionLibrary;

	void SnapshotActor(AActor* TargetActor);

public:
	// Map of Actor Snapshots mapping from the object path to the actual snapshot
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	TMap<FString, FLevelSnapshot_Actor> ActorSnapshots;

	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	FString MapName;
};