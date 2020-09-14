// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Containers/Array.h"

class AActor;
class UWorldPartition;
class UHLODLayer;
class AWorldPartitionHLOD;

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
	 * @param 	InCellName			The name of the cell
	 * @param 	InCellBounds		Bounds of the cell, will be assigned to the created HLOD actors.
	 * @param 	InCellLoadingRange	The range at which the cell should be loaded. This is the distance where the HLOD actors should fade out.
	 * @param 	InHLODLayer			The HLODLayer which will provide the HLOD building parameters.
	 * @param 	InSubActors			The actors from which we'll gather geometry to generate an HLOD mesh
	 * @return The list of HLOD actors for this cell.
	 */
	static TArray<AWorldPartitionHLOD*> BuildHLODs(UWorldPartition* InWorldPartition, FName InCellName, FBox InCellBounds, float InCellLoadingRange, const UHLODLayer* InHLODLayer, const TArray<AActor*>& InSubActors);
};

#endif