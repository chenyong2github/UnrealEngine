// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LevelSnapshotsEditorFunctionLibrary.generated.h"

class ULevelSnapshot;

UCLASS()
class LEVELSNAPSHOTSEDITOR_API ULevelSnapshotsEditorFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:

	/**
	 * @brief Creates a new Level Snapshot from a template and immediately snapshots the current level.
	 * @param LevelSnapshot A template for a new level snapshot
	 * @param FileName The desired asset file name
	 * @param FolderPath The desired asset location
	 */
	UFUNCTION(BlueprintCallable, Category = "LevelSnapshots", meta=(DevelopmentOnly))
	static void SaveLevelSnapshotToDisk(ULevelSnapshot* LevelSnapshot, const FString FileName, const FString FolderPath);
};