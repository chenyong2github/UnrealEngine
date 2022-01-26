// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessingTypes.h"
#include "MassArchetypeTypes.h"

class UWorld;
class UMassEntitySubsystem;
struct FMassEntityHandle;

namespace UE::Mass::Utils
{

/** returns the current execution mode for the processors calculated from the world network mode */
MASSENTITY_API extern EProcessorExecutionFlags GetProcessorExecutionFlagsForWold(const UWorld& World);

/** 
 * Fills OutChunkCollections with per-archetype FMassArchetypeSubChunks instances. 
 * @param DuplicatesHandling used to inform the function whether to expect duplicates.
 */
MASSENTITY_API extern void CreateSparseChunks(const UMassEntitySubsystem& EntitySystem, const TConstArrayView<FMassEntityHandle> Entities
	, const FMassArchetypeSubChunks::EDuplicatesHandling DuplicatesHandling, TArray<FMassArchetypeSubChunks >& OutChunkCollections);

} // namespace UE::Mass::Utils