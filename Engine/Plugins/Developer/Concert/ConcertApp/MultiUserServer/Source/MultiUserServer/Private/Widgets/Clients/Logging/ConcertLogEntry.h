// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertTransportEvents.h"

using FConcertLogID = uint64;

struct FConcertLogEntry
{
	/** Unique log ID. Log IDs grow sequentially. */
	FConcertLogID LogId;

	/** The log we're describing */
	FConcertLog Log;
};
