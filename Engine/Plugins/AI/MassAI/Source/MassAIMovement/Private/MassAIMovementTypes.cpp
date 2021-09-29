// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassAIMovementTypes.h"

DEFINE_LOG_CATEGORY(LogMassNavigation);
DEFINE_LOG_CATEGORY(LogMassDynamicObstacle);

void FMassMovementConfig::Update()
{
	for (FMassMovementStyleConfig& Style : MovementStyles)
	{
		Style.MaxProbability = 0;
		for (FMassMovementStyleSpeed& Speed : Style.DesiredSpeeds)
		{
			Style.MaxProbability += Speed.Probability;
			Speed.ProbabilityThreshold = Style.MaxProbability;
		}
	}
}
