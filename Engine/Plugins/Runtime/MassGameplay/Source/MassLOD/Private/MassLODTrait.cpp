// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassLODTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "Engine/World.h"
#include "MassCommonFragments.h"
#include "MassSimulationLOD.h"


void UMassSimulationLODTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	BuildContext.AddFragmentWithDefaultInitializer<FDataFragment_Transform>();
	BuildContext.AddFragmentWithDefaultInitializer<FMassLODInfoFragment>();
	BuildContext.AddFragmentWithDefaultInitializer<FMassSimulationLODFragment>();
	BuildContext.AddFragmentWithDefaultInitializer<FDataFragment_MassSimulationLODInfo>();
	BuildContext.AddTag<FMassOffLODTag>();
	BuildContext.AddChunkFragment<FMassSimulationVariableTickChunkFragment>();
}
