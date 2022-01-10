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
	BuildContext.AddFragment<FDataFragment_AgentRadius>(); // From UMassProcessor_AgentMovement
	BuildContext.AddFragment<FMassNavigationEdgesFragment>();
	BuildContext.AddFragment<FDataFragment_Transform>(); // From UMassProcessor_Movement
	BuildContext.AddFragment<FMassVelocityFragment>(); // From UMassProcessor_Movement
	BuildContext.AddFragment<FMassSteeringFragment>();
	BuildContext.AddFragment<FMassMoveTargetFragment>();
}
