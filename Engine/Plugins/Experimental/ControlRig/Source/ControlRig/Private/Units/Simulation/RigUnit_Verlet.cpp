// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Simulation/RigUnit_Verlet.h"
#include "Units/RigUnitContext.h"

void FRigUnit_VerletIntegrateVector::Execute(const FRigUnitContext& Context)
{
	if (Context.State == EControlRigState::Init)
	{
		Point.Mass = 1.f;
		Position = Point.Position = Target;
		Velocity = Acceleration = Point.Velocity = FVector::ZeroVector;
		return;
	}

	float U = FMath::Clamp<float>(Blend * Context.DeltaTime, 0.f, 1.f);
	FVector Force = (Target - Point.Position) * FMath::Max(Strength, 0.0001f);
	FVector PreviousVelocity = Point.Velocity;
	Point = FControlRigSimulationLibrary::IntegrateVerlet(Point, Force, Blend, Damp, Context.DeltaTime);
	Acceleration = Point.Velocity - PreviousVelocity;
	Position = Point.Position;
	Velocity = Point.Velocity;
}
