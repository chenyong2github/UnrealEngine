// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Simulation/RigUnit_Verlet.h"
#include "Units/RigUnitContext.h"

void FRigUnit_VerletIntegrateVector::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		Position = AccumulatedPosition = Target;
		Velocity = Acceleration = AccumulatedVelocity = FVector::ZeroVector;
		return;
	}

	float U = FMath::Clamp<float>(Blend * Context.DeltaTime, 0.f, 1.f);
	FVector V = (Target - AccumulatedPosition) * FMath::Max(Strength, 0.0001f);
	V = FMath::Lerp<FVector>(AccumulatedVelocity, V, U) * FMath::Clamp<float>(1.f - Damp, 0.f, 1.f);
	Acceleration = AccumulatedVelocity - V;

	AccumulatedVelocity = V;
	AccumulatedPosition = AccumulatedPosition + AccumulatedVelocity * Context.DeltaTime;

	Position = AccumulatedPosition;
	Velocity = AccumulatedVelocity;
}
