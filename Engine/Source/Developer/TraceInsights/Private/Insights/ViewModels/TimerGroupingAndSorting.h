// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Insights/ViewModels/TimerNode.h"

////////////////////////////////////////////////////////////////////////////////////////////////////
// Grouping

/** Enumerates types of grouping or sorting for the timer nodes. */
enum class ETimerGroupingMode
{
	/** Creates a single group for all timers. */
	Flat,

	/** Creates one group for one letter. */
	ByName,

	/** Creates groups based on timer metadata group names. */
	ByMetaGroupName,

	/** Creates one group for each timer type. */
	ByType,

	ByTotalInclusiveTime,

	ByTotalExclusiveTime,

	ByInstanceCount,

	/** Invalid enum type, may be used as a number of enumerations. */
	InvalidOrMax,
};

/** Type definition for shared pointers to instances of ETimerGroupingMode. */
typedef TSharedPtr<ETimerGroupingMode> ETimerGroupingModePtr;

////////////////////////////////////////////////////////////////////////////////////////////////////
