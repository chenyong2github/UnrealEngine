// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LevelSnapshotsEditorFunctionLibrary.generated.h"

class ULevelSnapshot;
class ULevelSnapshotFilter;

UCLASS()
class LEVELSNAPSHOTSEDITOR_API ULevelSnapshotsEditorFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:

	/**
	 * @brief Creates a new Level Snapshot asset in the content browser based on an existing Level Snapshot.
	 * @param LevelSnapshot The Level Snapshot to use as the template for the asset
	 * @param FileName The desired asset file name
	 * @param FolderPath The desired asset location
	 */
	UFUNCTION(BlueprintCallable, Category = "LevelSnapshots", meta=(DevelopmentOnly))
	static void SaveLevelSnapshotToDisk(ULevelSnapshot* LevelSnapshot, const FString FileName, const FString FolderPath);

	/**
	 * @brief Creates a new Level Snapshot asset in the content browser and then captures the target world
	 * @param WorldContextObject Context object to determine which world to take the snapshot in
	 * @param FileName The desired asset file name
	 * @param FolderPath The desired asset location
	 */
	UFUNCTION(BlueprintCallable, Category = "LevelSnapshots", meta = (DevelopmentOnly, WorldContext = "WorldContextObject"))
	static ULevelSnapshot* TakeLevelSnapshotAndSaveToDisk(const UObject* WorldContextObject, const FString FileName, const FString FolderPath, const FString Description);

	/**
	 * Uses TakeLevelSnapshotAndSaveToDisk() and assumes Editor World
	 */
	UFUNCTION(BlueprintCallable, Category = "LevelSnapshots")
	static void TakeAndSaveLevelSnapshotEditorWorld(const FString FileName, const FString FolderPath, const FString Description);

};