// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigUnit_SimBase.h"
#include "Kismet/KismetMathLibrary.h"
#include "RigUnit_SpringInterp.generated.h"

/**
 * Uses a simple spring model to interpolate a float from Current to Target.
 */
USTRUCT(meta=(DisplayName="Spring Interpolate", Keywords="Alpha,SpringInterpolate", PrototypeName = "SpringInterp"))
struct CONTROLRIG_API FRigUnit_SpringInterp : public FRigUnit_SimBase
{
	GENERATED_BODY()

	FRigUnit_SpringInterp()
	{
		Current = Target = Result = 0.0f;
		Stiffness = 10.0f;
		CriticalDamping = 2.0f;
		TargetVelocityAmount = 0.0f;
		SpringState = FFloatSpringState();
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/** Current position of the spring. */
	UPROPERTY(meta = (Input))
	float Current;

	/** Rest position of the spring. */
	UPROPERTY(meta=(Input))
	float Target;

	/** The spring stiffness determines how hard it will pull towards the target. */
	UPROPERTY(meta=(Input))
	float Stiffness;

	/** 
	 * Set it smaller than 1 to make the spring oscillate before stabilizing on the target. 
	 * Set it equal to 1 to reach the target without overshooting. 
	 * Set it higher than one to make the spring take longer to reach the target.
	 */
	UPROPERTY(meta=(Input))
	float CriticalDamping;

	/**
	 * If 1, target changes turn into position offsets and the target is followed with less delay. As it gets close to
	 * zero, the effect is disabled and only spring pull will affect the output.
	 */
	UPROPERTY(meta = (Input))
	float TargetVelocityAmount;

	/** New position of the spring after delta time. */
	UPROPERTY(meta=(Output))
	float Result;

	UPROPERTY()
	FFloatSpringState SpringState;
};

/**
 * Uses a simple spring model to interpolate a vector from Current to Target.
 */
USTRUCT(meta=(DisplayName="Spring Interpolate", Keywords="Alpha,SpringInterpolate", PrototypeName = "SpringInterp", MenuDescSuffix = "(Vector)"))
struct CONTROLRIG_API FRigUnit_SpringInterpVector : public FRigUnit_SimBase
{
	GENERATED_BODY()
	
	FRigUnit_SpringInterpVector()
	{
		Current = Target = Result = FVector::ZeroVector;
		Stiffness = 10.0f;
		CriticalDamping = 2.0f;
		TargetVelocityAmount = 0.0f;
		SpringState = FVectorSpringState();
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/** Current position of the spring. */
	UPROPERTY(meta = (Input))
	FVector Current;

	/** Rest position of the spring. */
	UPROPERTY(meta=(Input))
	FVector Target;

	/** The spring stiffness determines how hard it will pull towards the target. */
	UPROPERTY(meta=(Input))
	float Stiffness;

	/**
	 * Set it smaller than 1 to make the spring oscillate before stabilizing on the target.
	 * Set it equal to 1 to reach the target without overshooting.
	 * Set it higher than one to make the spring take longer to reach the target.
	 */
	UPROPERTY(meta=(Input))
	float CriticalDamping;

	/** 
	 * If 1, target changes turn into position offsets and the target is followed with less delay. As it gets close to 
	 * zero, the effect is disabled and only spring pull will affect the output. 
	 */
	UPROPERTY(meta = (Input))
	float TargetVelocityAmount;

	/** New position of the spring after delta time. */
	UPROPERTY(meta=(Output))
	FVector Result;

	UPROPERTY()
	FVectorSpringState SpringState;
};