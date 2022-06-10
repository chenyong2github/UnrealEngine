// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReverseMotorSimComponent.h"

void UReverseMotorSimComponent::Update(FAudioMotorSimInputContext& Input, FAudioMotorSimRuntimeContext& RuntimeInfo)
{
	if (Input.ForwardSpeed >= 0.f)
	{
		return;
	}

	Input.MotorFrictionModifier *= ReverseEngineResistanceModifier;
}
