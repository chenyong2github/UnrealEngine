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

class FWorldPartitionPackageCache;
class UWorldPartition;

class FWorldPartitionLevelHelper
{
public:
	static ULevel* CreateEmptyLevelForRuntimeCell(const UWorld* InWorld, const FString& InWorldAssetName, UPackage* DestPackage = nullptr);
	static void MoveExternalActorsToLevel(const TArray<FWorldPartitionRuntimeCellObjectMapping>& InChildPackages, ULevel* InLevel);
	static void RemapLevelSoftObjectPaths(ULevel* InLevel, UWorldPartition* InWorldPartition);
	
	static bool LoadActors(ULevel* InDestLevel, TArrayView<FWorldPartitionRuntimeCellObjectMapping> InActorPackages, FWorldPartitionPackageCache& InPackageCache, TFunction<void(bool)> InCompletionCallback, bool bLoadForPlay, bool bLoadAsync = true, FLinkerInstancingContext* InOutInstancingContext = nullptr);
private:
	static UWorld::InitializationValues GetWorldInitializationValues();
};

#endif