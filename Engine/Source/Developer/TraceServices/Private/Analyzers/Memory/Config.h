// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace Trace {
namespace TraceServices {

namespace FTrackerConfig
{
	static const uint32 NumLanes			= 32;
	static const uint32 MaxLaneInputItems	= 16 << 10;
	static const uint32 ActiveSetPageSize	= 32 << 10;

	/* The following are assumptions for optimisation and not configurable */
	static const uint32 MaxSerialBits		= 20;	// = 64 - addressable_bits - min_align = 64 - 47 - 3

	static_assert(MaxLaneInputItems < ActiveSetPageSize, "");
	static_assert(MaxLaneInputItems * NumLanes <= 1 << MaxSerialBits, "");
};

} // namespace TraceServices
} // namespace Trace

/* vim: set noet : */
