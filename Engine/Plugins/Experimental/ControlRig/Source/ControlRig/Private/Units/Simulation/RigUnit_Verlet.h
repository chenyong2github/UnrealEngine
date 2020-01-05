// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Simulation/RigUnit_SimBase.h"
#include "Math/Simulation/CRSimPoint.h"
#include "RigUnit_Verlet.generated.h"

/**
 * Simulates a single position over time using verlet integration
 */
USTRUCT(meta=(DisplayName="Verlet (Vector)", PrototypeName="Verlet", Keywords="Simulate,Integrate"))
struct FRigUnit_VerletIntegrateVector : public FRigUnit_SimBase
{
	GENERATED_BODY()
	
	FRigUnit_VerletIntegrateVector()
	{
		Target = Position = Velocity = Acceleration = FVector::ZeroVector;
		Strength = 64.f;
		Damp = 0.01;
		Blend = 5.f;
		Point = FCRSimPoint();
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FVector Target;

	/** The strength of the verlet spring */
	UPROPERTY(meta = (Input))
	float Strength;

	/** The amount of damping to apply ( 0.0 to 1.0, but usually really low like 0.005 )*/
	UPROPERTY(meta = (Input))
	float Damp;

	/** The amount of blending to apply per second */
	UPROPERTY(meta = (Input))
	float Blend;

	UPROPERTY(meta = (Output))
	FVector Position;

	UPROPERTY(meta = (Output))
	FVector Velocity;

	UPROPERTY(meta = (Output))
	FVector Acceleration;

	UPROPERTY()
	FCRSimPoint Point;
};

