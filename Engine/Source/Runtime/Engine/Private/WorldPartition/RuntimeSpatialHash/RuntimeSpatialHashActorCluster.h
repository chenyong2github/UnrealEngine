// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"

#if WITH_EDITOR

// Clustering
struct FActorCluster
{
	TSet<FGuid>					Actors;
	EActorGridPlacement			GridPlacement;
	FName						RuntimeGrid;
	FBox						Bounds;
	TArray<const UDataLayer*>	DataLayers;
	FDataLayersID				DataLayersID;

	FActorCluster(const FWorldPartitionActorDesc* InActorDesc, EActorGridPlacement InGridPlacement, UWorld* InWorld);
	void Add(const FActorCluster& InActorCluster);
};

typedef TFunctionRef<bool(const FWorldPartitionActorDesc&)> FFilterPredicate;

TArray<FActorCluster> CreateActorClusters(UWorldPartition* WorldPartition, const FFilterPredicate& InFilterPredicate);
TArray<FActorCluster> CreateActorClusters(UWorldPartition* WorldPartition);

#endif // #if WITH_EDITOR
