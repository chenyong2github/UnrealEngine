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
		LinearDamping = 0.01f;
		Position = LinearVelocity = FVector::ZeroVector;
	}

	/**
	 * The mass of the point
	 */
	UPROPERTY()
	float Mass;

	/**
	 * The linear damping of the point
	 */
	UPROPERTY()
	float LinearDamping;

	/**
	 * The position of the point
	 */
	UPROPERTY()
	FVector Position;

	/**
	 * The velocity of the point per second
	 */
	UPROPERTY()
	FVector LinearVelocity;
};

UENUM()
enum class EControlRigSimulationConstraintType : uint8
{
	Distance,
	DistanceFromA,
	DistanceFromB,
	Plane
};

USTRUCT()
struct FControlRigSimulationPointConstraint
{
	GENERATED_BODY()

	FControlRigSimulationPointConstraint()
	{
		Type = EControlRigSimulationConstraintType::Distance;
		SubjectA = SubjectB = INDEX_NONE;
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
	int32 SubjectA;

	/**
	 * The (optional) second point affected by this constraint
	 * This is currently only used for the distance constraint
	 */
	UPROPERTY()
	int32 SubjectB;

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
struct FControlRigSimulationLinearSpring
{
	GENERATED_BODY()

	FControlRigSimulationLinearSpring()
	{
		SubjectA = SubjectB = INDEX_NONE;
		AnchorA = AnchorB = FVector::ZeroVector;
		Coefficient = 32.f;
		Equilibrium = 100.f;
	}

	/**
	 * The first point affected by this spring
	 */
	UPROPERTY()
	int32 SubjectA;

	/**
	 * The second point affected by this spring
	 */
	UPROPERTY()
	int32 SubjectB;

	/**
	 * The anchor in the space of the first subject
	 */
	UPROPERTY()
	FVector AnchorA;

	/**
	 * The anchor in the space of the second subject
	 */
	UPROPERTY()
	FVector AnchorB;

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

USTRUCT(meta = (Abstract))
struct FControlRigSimulationContainer
{
	GENERATED_BODY()

	FControlRigSimulationContainer()
	{
		TimeStep = 1.f / 60.f;
		AccumulatedTime = 0.f;
		TimeLeftForStep = 0.f;
	}
	virtual ~FControlRigSimulationContainer() {}

	/**
	 * The time step used by this container
	 */
	UPROPERTY()
	float TimeStep;

	/**
	 * The time step used by this container
	 */
	UPROPERTY()
	float AccumulatedTime;

	/**
	 * The time left until the next step
	 */
	UPROPERTY()
	float TimeLeftForStep;

	virtual void Reset();
	virtual void ResetTime();
	virtual void StepVerlet(float InDeltaTime, float InBlend);
	virtual void StepSemiExplicitEuler(float InDeltaTime);

protected:
	virtual void CachePreviousStep() {};
	virtual void IntegrateVerlet(float InBlend) {};
	virtual void IntegrateSemiExplicitEuler() {};

	friend class FControlRigSimulationLibrary;
};

USTRUCT()
struct FControlRigSimulationPointContainer : public FControlRigSimulationContainer
{
	GENERATED_BODY()

	FControlRigSimulationPointContainer()
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
	TArray<FControlRigSimulationLinearSpring> Springs;

	/**
	 * The constraints within the simulation
	 */
	UPROPERTY()
	TArray<FControlRigSimulationPointConstraint> Constraints;

	FControlRigSimulationPoint GetPointInterpolated(int32 InIndex) const;

	virtual void Reset() override;
	virtual void ResetTime() override;

protected:

	UPROPERTY()
	TArray<FControlRigSimulationPoint> PreviousStep;

	virtual void CachePreviousStep() override;
	virtual void IntegrateVerlet(float InBlend) override;
	virtual void IntegrateSemiExplicitEuler() override;
	void IntegrateSprings();
	void IntegrateVelocityVerlet(float InBlend);
	void IntegrateVelocitySemiExplicitEuler();
	void ApplyConstraints();

	friend class FControlRigSimulationLibrary;
};

class CONTROLRIG_API FControlRigSimulationLibrary
{
public:
	static FControlRigSimulationPoint IntegrateVerlet(const FControlRigSimulationPoint& InPoint, const FVector& InForce, float InBlend, float InDeltaTime);
	static FControlRigSimulationPoint IntegrateSemiExplicitEuler(const FControlRigSimulationPoint& InPoint, const FVector& InForce, float InDeltaTime);

	static void IntegrateVerlet(FControlRigSimulationPointContainer& InOutSimulation, float InBlend, float InDeltaTime);
	static void IntegrateSemiExplicitEuler(FControlRigSimulationPointContainer& InOutSimulation, float InDeltaTime);

	static void ComputeWeightsFromMass(float MassA, float MassB, float& OutWeightA, float& OutWeightB);
	static void ComputeLinearSpring(const FControlRigSimulationPoint& InPointA, const FControlRigSimulationPoint& InPointB, const FControlRigSimulationLinearSpring& InSpring, FVector& ForceA, FVector& ForceB);
	static void ApplyPointConstraint(const FControlRigSimulationPointConstraint& InConstraint, FControlRigSimulationPoint& OutPointA, FControlRigSimulationPoint& OutPointB);
};