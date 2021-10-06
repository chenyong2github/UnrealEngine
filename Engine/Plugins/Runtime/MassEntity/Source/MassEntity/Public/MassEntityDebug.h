// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VisualLogger/VisualLoggerTypes.h"
#include "MassProcessingTypes.h"
#include "MassEntityQuery.h"

class FOutputDevice;
class UMassProcessor;

MASSENTITY_API DECLARE_ENUM_TO_STRING(EMassProcessingPhase);

namespace UE::Mass::Debug
{
	FString DebugGetFragmentAccessString(EMassFragmentAccess Access);
	MASSENTITY_API extern void DebugOutputDescription(TConstArrayView<UMassProcessor*> Processors, FOutputDevice& Ar);
} // namespace UE::Mass::Debug

