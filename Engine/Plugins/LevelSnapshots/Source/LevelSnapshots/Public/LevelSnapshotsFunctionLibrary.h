// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LevelSnapshotsFunctionLibrary.generated.h"

class ULevelSnapshot;
class ULevelSnapshotFilter;

UCLASS()
class LEVELSNAPSHOTS_API ULevelSnapshotsFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:

	UFUNCTION(BlueprintCallable, Category = "LevelSnapshots", meta = (WorldContext = "WorldContextObject"))
	static ULevelSnapshot* TakeLevelSnapshot(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "LevelSnapshots", meta = (WorldContext = "WorldContextObject"))
	static void ApplySnapshotToWorld(const UObject* WorldContextObject, const ULevelSnapshot* Snapshot, const ULevelSnapshotFilter* Filter = nullptr);

	UFUNCTION(BlueprintCallable, Category = "Snapshot")
	static void TestDeserialization(const ULevelSnapshot* Snapshot, AActor* TestActor);

	UFUNCTION(BlueprintCallable, Category = "Snapshot")
	static void DiffSnapshots(const ULevelSnapshot* FirstSnapshot, const ULevelSnapshot* SecondSnapshot);
};