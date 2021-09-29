// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassZoneGraphAnnotationFragments.h"
#include "EntitySubsystem.h"
#include "MassSimulationLOD.h"

namespace UE::Mass::ZoneGraphAnnotations
{
// Update interval range for periodic annotation tag update.
static const float MinUpdateInterval = 0.25f;
static const float MaxUpdateInterval = 0.5f;

// Update interval range for periodic annotation tag update for Off LOD.
static const float OffLODMinUpdateInterval = 1.905f;
static const float OffLODMaxUpdateInterval = 2.10f;

} // UE::Mass::ZoneGraphAnnotations

//----------------------------------------------------------------------//
//  FMassZoneGraphAnnotationVariableTickChunkFragment
//----------------------------------------------------------------------//

bool FMassZoneGraphAnnotationVariableTickChunkFragment::UpdateChunk(FLWComponentSystemExecutionContext& Context)
{
	FMassZoneGraphAnnotationVariableTickChunkFragment& ChunkFrag = Context.GetMutableChunkComponent<FMassZoneGraphAnnotationVariableTickChunkFragment>();
	ChunkFrag.TimeUntilNextTick -= Context.GetDeltaTimeSeconds();
	if (ChunkFrag.TimeUntilNextTick <= 0.0f)
	{
		// @todo Possible future optimization, right now it is faster but not by a whole lot.
		if (FMassSimulationVariableTickChunkFragment::GetChunkLOD(Context) == EMassLOD::Off)
		{
			ChunkFrag.TimeUntilNextTick = FMath::RandRange(UE::Mass::ZoneGraphAnnotations::OffLODMinUpdateInterval, UE::Mass::ZoneGraphAnnotations::OffLODMaxUpdateInterval);
		}
		else
		{
			ChunkFrag.TimeUntilNextTick = FMath::RandRange(UE::Mass::ZoneGraphAnnotations::MinUpdateInterval, UE::Mass::ZoneGraphAnnotations::MaxUpdateInterval);
		}
		return true;
	}

	return false;
}
