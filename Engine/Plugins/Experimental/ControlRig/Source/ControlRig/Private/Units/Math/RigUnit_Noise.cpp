// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_Noise.h"
#include "Units/RigUnitContext.h"

FRigUnit_NoiseFloat_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (Context.State == EControlRigState::Init)
	{
		Time = 0.f;
		return;
	}

	float Noise = FMath::PerlinNoise1D(Value * Frequency + Time) + 0.5f;
	Result = FMath::Lerp<float>(Minimum, Maximum, Noise);
	Time = Time + Speed * Context.DeltaTime;
}

FRigUnit_NoiseVector_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if (Context.State == EControlRigState::Init)
	{
		Time = FVector::ZeroVector;
		return;
	}

	float NoiseX = FMath::PerlinNoise1D(Position.X * Frequency.X + Time.X) + 0.5f;
	float NoiseY = FMath::PerlinNoise1D(Position.Y * Frequency.Y + Time.Y) + 0.5f;
	float NoiseZ = FMath::PerlinNoise1D(Position.Z * Frequency.Z + Time.Z) + 0.5f;
	Result.X = FMath::Lerp<float>(Minimum, Maximum, NoiseX);
	Result.Y = FMath::Lerp<float>(Minimum, Maximum, NoiseY);
	Result.Z = FMath::Lerp<float>(Minimum, Maximum, NoiseZ);
	Time = Time + Speed * Context.DeltaTime;
}
