// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DiffUtils.h"
#include "LevelSnapshotsFunctionLibrary.generated.h"

class ULevelSnapshot;
class ULevelSnapshotFilter;


USTRUCT(BlueprintType)
struct FLevelSnapshot_ComponentDiff
{
	GENERATED_BODY()

	FLevelSnapshot_ComponentDiff() = default;

	UPROPERTY(BlueprintReadOnly, Category = "Snapshot")
	TArray<FString> ModifiedPropertyPaths;
};

USTRUCT(BlueprintType)
struct FLevelSnapshot_ActorDiff
{
	GENERATED_BODY()

	FLevelSnapshot_ActorDiff() = default;

	FLevelSnapshot_ActorDiff(const TArray<FString>& InModifiedPropertyPaths)
		: ModifiedPropertyPaths(InModifiedPropertyPaths)
	{	};

	UPROPERTY(BlueprintReadOnly, Category = "Snapshot")
	TMap<FString, FLevelSnapshot_ComponentDiff> ModifiedComponents;

	UPROPERTY(BlueprintReadOnly, Category="Snapshot")
	TArray<FString> ModifiedPropertyPaths;
};

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
	static void DiffSnapshots(const ULevelSnapshot* FirstSnapshot, const ULevelSnapshot* SecondSnapshot, TMap<FString, FLevelSnapshot_ActorDiff>& DiffResults);
};