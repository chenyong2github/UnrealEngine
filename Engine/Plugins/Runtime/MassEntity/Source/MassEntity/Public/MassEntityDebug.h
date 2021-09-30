// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "VisualLogger/VisualLoggerTypes.h"
#include "MassProcessingTypes.h"

class FOutputDevice;
class UMassProcessor;

MASSENTITY_API DECLARE_ENUM_TO_STRING(EMassProcessingPhase);

namespace UE { namespace Pipe { namespace Debug {

MASSENTITY_API extern void DebugOutputDescription(TConstArrayView<UMassProcessor*> Processors, FOutputDevice& Ar);

}}} // namespace UE::Pipe::Debug

