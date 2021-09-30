// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassLODUtils.h"

DEFINE_LOG_CATEGORY(LogMassLOD);
#if WITH_MASSGAMEPLAY_DEBUG
namespace UE::MassLOD::Debug
{
	bool bLODCalculationsPaused = false;
	
	FAutoConsoleVariableRef CVarLODPause(TEXT("mass.lod.pause"), bLODCalculationsPaused, TEXT("If non zero will pause all LOD calculations"));
} // UE::MassLOD::Debug
#endif // WITH_MASSGAMEPLAY_DEBUG