// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassAvoidanceTrait.h"
#include "MassEntityTemplateRegistry.h"
#include "Engine/World.h"
#include "MassAvoidanceProcessors.h"
#include "MassCommonFragments.h"
#include "MassAIMovementFragments.h"


void UMassAvoidanceTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	// UMassAvoidanceProcessor
	BuildContext.AddFragmentWithDefaultInitializer<FDataFragment_AgentRadius>(); // From UMassProcessor_AgentMovement
	BuildContext.AddFragmentWithDefaultInitializer<FMassNavigationEdgesFragment>();
	BuildContext.AddFragmentWithDefaultInitializer<FDataFragment_Transform>(); // From UMassProcessor_Movement
	BuildContext.AddFragmentWithDefaultInitializer<FMassVelocityFragment>(); // From UMassProcessor_Movement
	BuildContext.AddFragmentWithDefaultInitializer<FMassSteeringFragment>();
	BuildContext.AddFragmentWithDefaultInitializer<FMassMoveTargetFragment>();
}
