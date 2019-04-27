// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Math/ControlRigSimulationLibrary.h"

FControlRigSimulationPoint FControlRigSimulationLibrary::IntegrateVerlet(const FControlRigSimulationPoint& InPoint, const FVector& InForce, float InBlend, float InDamping, float InDeltaTime)
{
	FControlRigSimulationPoint Point = InPoint;
	if(Point.Mass > SMALL_NUMBER)
	{
		Point.Velocity = FMath::Lerp<FVector>(Point.Velocity, InForce, FMath::Clamp<float>(InBlend * InDeltaTime, 0.f, 1.f)) * FMath::Clamp<float>(1.f - InDamping, 0.f, 1.f);
		Point.Position = Point.Position + Point.Velocity * InDeltaTime;
	}
	return Point;
}

FControlRigSimulationPoint FControlRigSimulationLibrary::IntegrateSemiExplicitEuler(const FControlRigSimulationPoint& InPoint, const FVector& InForce, float InDamping, float InDeltaTime)
{
	FControlRigSimulationPoint Point = InPoint;
	if(Point.Mass > SMALL_NUMBER)
	{
		Point.Velocity += InForce * InDeltaTime;
		Point.Velocity -= InPoint.Velocity * InDamping;
		Point.Position += Point.Velocity * InDeltaTime;
	}
	return Point;
}

void FControlRigSimulationLibrary::IntegrateVerlet(FControlRigSimulationContainer& InOutSimulation, float InBlend, float InDamping, float InDeltaTime)
{
	ensure(false); // to be implemented
}

void FControlRigSimulationLibrary::IntegrateSemiExplicitEuler(FControlRigSimulationContainer& InOutSimulation, float InDamping, float InDeltaTime)
{
	ensure(false); // to be implemented
}