// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ControlRigSimulationLibrary.generated.h"

USTRUCT()
struct CONTROLRIG_API FControlRigSimulationPoint
{
	GENERATED_BODY()

	FControlRigSimulationPoint()
	{
		Mass = 1.f;
		Position = Velocity = FVector::ZeroVector;
	}

	/**
	 * The mass of the point
	 */
	UPROPERTY()
	float Mass;

	/**
	 * The position of the point
	 */
	UPROPERTY()
	FVector Position;

	/**
	 * The velocity of the point per second
	 */
	UPROPERTY()
	FVector Velocity;
}

UENUM()
enum class CONTROLRIG_API EControlRigSimulationConstraintType : uint8
{
	Distance,
	Plane
};

USTRUCT()
struct CONTROLRIG_API FControlRigSimulationConstraint
{
	GENERATED_BODY()

	FControlRigSimulationConstraint()
	{
		Type = EControlRigSimulationConstraintType::Pinned;
		PointA = PointB = INDEX_NONE;
		DataA = DataB = FVector::ZeroVector;
	}

	/**
	 * The type of the constraint
	 */
	UPROPERTY()
	EControlRigSimulationConstraintType Type;

	/**
	 * The first point affected by this constraint
	 */
	UPROPERTY()
	int32 PointA;

	/**
	 * The (optional) second point affected by this constraint
	 * This is currently only used for the distance constraint
	 */
	UPROPERTY()
	int32 PointB;

	/**
	 * The first data member for the constraint.
	 */
	UPROPERTY()
	FVector DataA;

	/**
	 * The second data member for the constraint.
	 */
	UPROPERTY()
	FVector DataB;
};

USTRUCT()
struct CONTROLRIG_API FControlRigSimulationSpring
{
	GENERATED_BODY()

	FControlRigSimulationSpring()
	{
		Type = FControlRigSimulationSpring::Pinned;
		PointA = PointB = INDEX_NONE;
		Coefficient = 32.f;
		Equilibrium = 100.f;
	}

	/**
	 * The type of the constraint
	 */
	UPROPERTY()
	EControlRigSimulationConstraintType Type;

	/**
	 * The first point affected by this constraint
	 */
	UPROPERTY()
	int32 PointA;

	/**
	 * The (optional) second point affected by this constraint
	 * This is currently only used for the distance constraint
	 */
	UPROPERTY()
	int32 PointB;

	/**
	 * The power of this spring
	 */
	UPROPERTY()
	float Coefficient;

	/**
	 * The rest length of this spring
	 */
	UPROPERTY()
	float Equilibrium;
};

USTRUCT()
struct CONTROLRIG_API FControlRigSimulationContainer
{
	GENERATED_BODY()

	FControlRigSimulationContainer()
	{
	}

	/**
	 * The points within the simulation
	 */
	UPROPERTY()
	TArray<FControlRigSimulationPoint> Points;

	/**
	 * The springs within the simulation
	 */
	UPROPERTY()
	TArray<FControlRigSimulationSpring> Springs;

	/**
	 * The constraints within the simulation
	 */
	UPROPERTY()
	TArray<FControlRigSimulationConstraint> Constraints;
};

class CONTROLRIG_API FControlRigSimulationLibrary
{
public:
	static FControlRigSimulationPoint IntegrateVerlet(const FControlRigSimulationPoint& InPoint, const FVector& InForce, float InBlend, float InDamping, float InDeltaTime);
	static FControlRigSimulationPoint IntegrateExplicitEuler(const FControlRigSimulationPoint& InPoint, const FVector& InForce, float InDamping, float InDeltaTime);
};