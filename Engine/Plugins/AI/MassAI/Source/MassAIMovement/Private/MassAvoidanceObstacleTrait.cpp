// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassAvoidanceObstacleTrait.h"

#include "MassAvoidanceProcessors.h"
#include "MassEntityTemplateRegistry.h"
#include "Engine/World.h"
#include "MassAIMovementFragments.h"
#include "MovementProcessor.h"

void UMassAvoidanceObstacleTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, UWorld& World) const
{
	// UAvoidanceObstacleProcessor
	BuildContext.AddFragment<FMassAvoidanceObstacleGridCellLocationFragment>();
	BuildContext.AddFragment<FDataFragment_AgentRadius>();

	if (bExtendToEdgeObstacle)
	{
		BuildContext.AddTag<FMassAvoidanceExtendToEdgeObstacleTag>();
	}
}