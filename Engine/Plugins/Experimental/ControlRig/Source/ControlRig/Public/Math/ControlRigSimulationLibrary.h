// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ControlRigSimulationLibrary.generated.h"

USTRUCT()
struct FControlRigSimulationPoint
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
};

UENUM()
enum class EControlRigSimulationConstraintType : uint8
{
	Distance,
	Plane
};

USTRUCT()
struct FControlRigSimulationConstraint
{
	GENERATED_BODY()

	FControlRigSimulationConstraint()
	{
		Type = EControlRigSimulationConstraintType::Distance;
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
struct FControlRigSimulationSpring
{
	GENERATED_BODY()

	FControlRigSimulationSpring()
	{
		PointA = PointB = INDEX_NONE;
		Coefficient = 32.f;
		Equilibrium = 100.f;
	}

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
struct FControlRigSimulationContainer
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

private:

	UPROPERTY()
	TArray<FControlRigSimulationPoint> PreviousStep;

	friend class FControlRigSimulationLibrary;
};

class CONTROLRIG_API FControlRigSimulationLibrary
{
public:
	static FControlRigSimulationPoint IntegrateVerlet(const FControlRigSimulationPoint& InPoint, const FVector& InForce, float InBlend, float InDamping, float InDeltaTime);
	static FControlRigSimulationPoint IntegrateSemiExplicitEuler(const FControlRigSimulationPoint& InPoint, const FVector& InForce, float InDamping, float InDeltaTime);

	static void IntegrateVerlet(FControlRigSimulationContainer& InOutSimulation, float InBlend, float InDamping, float InDeltaTime);
	static void IntegrateSemiExplicitEuler(FControlRigSimulationContainer& InOutSimulation, float InDamping, float InDeltaTime);
};