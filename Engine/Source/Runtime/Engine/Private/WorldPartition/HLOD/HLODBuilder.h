// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Containers/Array.h"

class AActor;
class UWorldPartition;
class UHLODLayer;
class AWorldPartitionEditorCellPreview;

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
	 * @param 	InHLODLayer			The HLODLayer which will provide the HLOD building parameters.
	 * @param 	InSubActors			The actors from which we'll gather geometry to generate an HLOD mesh
	 */
	static void BuildHLODs(UWorldPartition* InWorldPartition, FName InCellName, const UHLODLayer* InHLODLayer, const TArray<AActor*>& InSubActors);

	/**
	 * Generate a cell preview mesh from the given actors.
	 * 
	 * @param	InCellActors		Actors from which meshes will be gathered
	 * @param	InCellBounds		Bounds of the cell
	 * @return A new cell preview actor
	 */
	static AWorldPartitionEditorCellPreview* BuildCellPreviewMesh(const TArray<AActor*>& InCellActors, const FBox& InCellBounds);
};

#endif