// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessingTypes.h"

namespace UE::Mass::Utils
{

/** returns the current execution mode for the processors calculated from the world network mode */
MASSENTITY_API extern EProcessorExecutionFlags GetProcessorExecutionFlagsForWold(const UWorld& World);

} // namespace UE::Mass::Utils