// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LWComponentTypes.h"
#include "ZoneGraphTypes.h"
#include "Containers/StaticArray.h"
#include "MassLODManager.h"

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


