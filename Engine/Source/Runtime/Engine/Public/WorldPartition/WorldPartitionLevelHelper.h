// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * WorldPartitionLevelHelper
 *
 * Helper class to build Levels for World Partition
 *
 */

#pragma once

#if WITH_EDITOR

#include "Engine/World.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"

class FWorldPartitionLevelHelper
{
public:
	static const TCHAR* GetSavedLevelOutputSubFolderName() { return TEXT("WorldPartitionLevels"); }
	static bool CreateAndSaveLevelForRuntimeCell(const UWorld* InWorld, const FString& InWorldAssetName, const TArray<FWorldPartitionRuntimeCellObjectMapping>& InChildPackages);
	static ULevel* CreateEmptyLevelForRuntimeCell(const UWorld* InWorld, const FString& InWorldAssetName);
private:
	static UWorld::InitializationValues GetWorldInitializationValues();
};
#endif