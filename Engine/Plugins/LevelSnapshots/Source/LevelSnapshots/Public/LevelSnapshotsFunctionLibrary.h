// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LevelSnapshotsFunctionLibrary.generated.h"

class ULevelSnapshot;

UCLASS()
class LEVELSNAPSHOTS_API ULevelSnapshotsFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:

	UFUNCTION(BlueprintCallable, Category = "LevelSnapshots", meta = (WorldContext = "WorldContextObject"))
	static ULevelSnapshot* TakeLevelSnapshot(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Snapshot")
	static void TestDeserialization(const ULevelSnapshot* Snapshot, AActor* TestActor);
};