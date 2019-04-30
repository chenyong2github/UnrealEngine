// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Math/ControlRigVerletLibrary.h"

FControlRigVerletPoint ControlRigVerletLibrary::IntegrateVerlet(const FControlRigSimulationPoint& InPoint, const FVector& InForce, float InBlend, float InDamping, float InDeltaTime)
{
	FControlRigVerletPoint Point = InPoint;
	Point.Velocity = FMath::Lerp<FVector>(Point.Velocity, Force, FMath::Clamp<float>(InBlend * InDeltaTime, 0.f, 1.f)) * FMath::Clamp<float>(1.f - InDamping, 0.f, 1.f);
	Point.Position = Point.Position + Point.Velocity * InDeltaTime;
	return Point;
}

FControlRigSimulationPoint ControlRigVerletLibrary::IntegrateExplicitEuler(const FControlRigSimulationPoint& InPoint, const FVector& InForce, float InDamping, float InDeltaTime)
{
	FControlRigVerletPoint Point = InPoint;
	Point.Velocity += InForce * InDeltaTime;
	Point.Velocity -= InPoint.Velocity * InDamping;
	Point.Position += Point.Velocity * InDeltaTime;
	return Point;
}
