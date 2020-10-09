// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ActorSnapshot.h"
#include "CoreMinimal.h"

#include "LevelSnapshot.generated.h"

UCLASS()
class LEVELSNAPSHOTS_API ULevelSnapshot : public UObject
{
	GENERATED_BODY()

public:

	void SnapshotWorld(UWorld* TargetWorld);

private:

	void SnapshotActor(AActor* TargetActor);

	// Map of Actor Snapshots mapping from the object path to the actual snapshot
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	TMap<FString, FActorSnapshot> ActorSnapshots;

public:
	UPROPERTY(VisibleAnywhere, Category = "Snapshot")
	FString MapName;
};