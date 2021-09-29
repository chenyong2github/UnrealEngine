// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassAvoidanceTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "Engine/World.h"
#include "MassAvoidanceProcessors.h"
#include "MassCommonFragments.h"
#include "MassAIMovementFragments.h"
#include "MassSimulationLOD.h"
#include "MassZoneGraphMovementFragments.h"

void UMassAvoidanceTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	// UMassAvoidanceProcessor
	BuildContext.AddFragmentWithDefaultInitializer<FDataFragment_AgentRadius>(); // From UMassProcessor_AgentMovement
	BuildContext.AddFragmentWithDefaultInitializer<FMassNavigationEdgesFragment>();
	BuildContext.AddFragmentWithDefaultInitializer<FMassMoveTargetFragment>();
	BuildContext.AddFragmentWithDefaultInitializer<FDataFragment_Transform>(); // From UMassProcessor_Movement
	BuildContext.AddFragmentWithDefaultInitializer<FMassVelocityFragment>(); // From UMassProcessor_Movement
	BuildContext.AddFragmentWithDefaultInitializer<FMassSteeringFragment>();
	
	// LOD: UMassSimulationLODProcessors
	BuildContext.AddFragmentWithDefaultInitializer<FDataFragment_Transform>();
	BuildContext.AddFragmentWithDefaultInitializer<FMassLODInfoFragment>();
	BuildContext.AddFragmentWithDefaultInitializer<FMassSimulationLODFragment>();
	BuildContext.AddChunkFragment<FMassSimulationVariableTickChunkFragment>();

	if (bUseZoneGraphMovement)
	{
		// UMassLaneCacheBoundaryProcessor
		BuildContext.AddFragmentWithDefaultInitializer<FMassZoneGraphCachedLaneFragment>();
		BuildContext.AddFragmentWithDefaultInitializer<FMassMoveTargetFragment>();
		BuildContext.AddFragmentWithDefaultInitializer<FMassZoneGraphLaneLocationFragment>();
		BuildContext.AddFragmentWithDefaultInitializer<FMassNavigationEdgesFragment>();
		BuildContext.AddFragmentWithDefaultInitializer<FMassLaneCacheBoundaryFragment>();
	}
	else
	{
		// Put non-ZG movement specific fragments here
		// UMassLaneBoundaryProcessor
		BuildContext.AddFragmentWithDefaultInitializer<FDataFragment_Transform>();
		BuildContext.AddFragmentWithDefaultInitializer<FMassNavigationEdgesFragment>();
		BuildContext.AddFragmentWithDefaultInitializer<FMassLastUpdatePositionFragment>();
		BuildContext.AddFragmentWithDefaultInitializer<FMassZoneGraphLaneLocationFragment>();
		BuildContext.AddFragmentWithDefaultInitializer<FMassAvoidanceBoundaryLastLaneHandleFragment>();
	}
}
