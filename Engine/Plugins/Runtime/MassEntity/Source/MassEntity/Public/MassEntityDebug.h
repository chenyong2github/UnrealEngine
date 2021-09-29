// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "VisualLogger/VisualLoggerTypes.h"
#include "MassEntityTypes.h"

class FOutputDevice;
class UPipeProcessor;

MASSENTITY_API DECLARE_ENUM_TO_STRING(EPipeProcessingPhase);

namespace UE { namespace Pipe { namespace Debug {

MASSENTITY_API extern void DebugOutputDescription(TConstArrayView<UPipeProcessor*> Processors, FOutputDevice& Ar);

}}} // namespace UE::Pipe::Debug

