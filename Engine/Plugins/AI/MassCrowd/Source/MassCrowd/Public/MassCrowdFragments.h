// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "ZoneGraphTypes.h"
#include "ZoneGraphAnnotationTypes.h"
#include "MassCrowdFragments.generated.h"

/**
 * Special tag to differentiate the crowd from the rest of the other entities
 * Should not contain any data, this is purely a tag
 */
USTRUCT()
struct MASSCROWD_API FTagFragment_MassCrowd : public FMassTag
{
	GENERATED_BODY()
};

/**
 * Data fragment to store the last lane the agent was tracked on.
 */
USTRUCT()
struct MASSCROWD_API FMassCrowdLaneTrackingFragment : public FMassFragment
{
	GENERATED_BODY()
	FZoneGraphLaneHandle TrackedLaneHandle;
};


USTRUCT()
struct MASSCROWD_API FMassCrowdObstacleFragment : public FMassFragment
{
	GENERATED_BODY()

	/** Obstacle ID reported to the obstruction annotation. */
	FMassLaneObstacleID LaneObstacleID;

	/** Position of the dynamic obstacle when it last moved. */
	FVector LastPosition = FVector::ZeroVector;

	/** Time stamp when that obstacle stopped moving. */
	float LastMovedTimeStamp = 0.0f;

	/** Has this dynamic obstacle stopped moving. */
	bool bHasStopped = true;
};
