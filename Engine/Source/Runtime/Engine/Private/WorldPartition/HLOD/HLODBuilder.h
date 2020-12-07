// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "WorldPartition/WorldPartitionHandle.h"

class AActor;
class UWorldPartition;
class UHLODLayer;
class AWorldPartitionHLOD;

struct ENGINE_API FHLODGenerationContext
{
	TMap<uint64, FWorldPartitionHandle> HLODActorDescs;
	TArray<FWorldPartitionReference> ActorReferences;
	
	// Everything needed to build the cell hash
	int64 GridIndexX;
	int64 GridIndexY;
	int64 GridIndexZ;
	FName HLODLayerName;
};

/**
 * Tools for building HLODs in WorldPartition
 */
class FHLODBuilderUtilities
{
public:
	/**
	 * Build HLODs for a given cell of a WorldPartition level.
	 * May spawn multiple AWorldPartitionHLOD actors depending on the HLODLayer settings.
	 *
	 * @param 	InWorldPartition	The WorldPartition for which we are building HLODs
	 * @param	InContext			The HLODs generation context
	 * @param 	InCellName			The name of the cell
	 * @param 	InCellBounds		Bounds of the cell, will be assigned to the created HLOD actors.
	 * @param 	InHLODLayer			The HLODLayer which will provide the HLOD building parameters.
	 * @param	InHLODLevel			Level of HLOD (HLOD 0...N)
	 * @param 	InSubActors			The actors from which we'll gather geometry to generate an HLOD mesh
	 * @return The list of HLOD actors for this cell.
	 */
	static TArray<AWorldPartitionHLOD*> BuildHLODs(UWorldPartition* InWorldPartition, FHLODGenerationContext* InContext, FName InCellName, const FBox& InCellBounds, const UHLODLayer* InHLODLayer, uint32 InHLODLevel, const TArray<const AActor*>& InSubActors);
};

#endif