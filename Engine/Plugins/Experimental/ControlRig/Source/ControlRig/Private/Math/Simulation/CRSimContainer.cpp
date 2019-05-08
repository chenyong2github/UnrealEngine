// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Math/Simulation/CRSimContainer.h"

void FCRSimContainer::Reset()
{
	AccumulatedTime = TimeLeftForStep = 0.f;
}

void FCRSimContainer::ResetTime()
{
	AccumulatedTime = TimeLeftForStep = 0.f;
}

void FCRSimContainer::StepVerlet(float InDeltaTime, float InBlend)
{
	TimeLeftForStep -= InDeltaTime;

	if (TimeLeftForStep >= 0.f)
	{
		return;
	}

	while (TimeLeftForStep < 0.f)
	{
		TimeLeftForStep += TimeStep;
		AccumulatedTime += TimeStep;
		CachePreviousStep();
		IntegrateVerlet(InBlend);
	}
}

void FCRSimContainer::StepSemiExplicitEuler(float InDeltaTime)
{
	TimeLeftForStep -= InDeltaTime;

	if (TimeLeftForStep >= 0.f)
	{
		return;
	}

	while (TimeLeftForStep < 0.f)
	{
		TimeLeftForStep += TimeStep;
		AccumulatedTime += TimeStep;
		CachePreviousStep();
		IntegrateSemiExplicitEuler();
	}
}
