// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/DataLayer/DataLayersID.h"
#include "WorldPartition/WorldPartitionActorCluster.h"

class AActor;
class UWorldPartition;
class UHLODLayer;
class AWorldPartitionHLOD;

struct ENGINE_API FHLODCreationContext
{
	TMap<uint64, FWorldPartitionHandle> HLODActorDescs;
	TArray<FWorldPartitionReference> ActorReferences;
};

struct ENGINE_API FHLODCreationParams
{
	UWorldPartition* WorldPartition;

	// Everything needed to build the cell hash
	int64 GridIndexX;
	int64 GridIndexY;
	int64 GridIndexZ;
	FDataLayersID DataLayersID;
	FName HLODLayerName;

	FName CellName;
	FBox  CellBounds;
	uint32 HLODLevel;
};

/**
 * Tools for building HLODs in WorldPartition
 */
class FHLODBuilderUtilities
{
public:
	/**
	 * Create HLOD actors for a given cell
	 * 
	 * @param	InCreationContext	HLOD creation context object
	 * @param	InCreationParams	HLOD creation parameters object
	 * @param	InActors			The actors for which we'll build an HLOD representation
	 * @param	InDataLayers		The data layers to assign to the newly created HLOD actors
	 */
	static TArray<AWorldPartitionHLOD*> CreateHLODActors(FHLODCreationContext& InCreationContext, const FHLODCreationParams& InCreationParams, const TSet<FActorInstance>& InActors, const TArray<const UDataLayer*>& InDataLayers);

	/**
	 * Build HLOD for the specified AWorldPartitionHLOD actor.
	 *
	 * @param 	InHLODActor		The HLOD actor for which we'll build the HLOD
	 * @return An hash that represent the content used to build this HLOD.
	 */
	static uint32 BuildHLOD(AWorldPartitionHLOD* InHLODActor);
};

#endif