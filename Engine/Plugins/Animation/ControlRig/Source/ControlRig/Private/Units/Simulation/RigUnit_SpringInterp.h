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
		bUseCurrentInput = true;
		Current = Target = Result = 0.0f;
		Strength = 4.0f;
		CriticalDamping = 1.0f;
		TargetVelocityAmount = 0.0f;
		bInitializeFromTarget = false;
		SpringState = FFloatSpringState();
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/** Rest/target position of the spring. */
	UPROPERTY(meta=(Input))
	float Target;

	/**
	 * If true, then the Current input will be used to initialize the state, and is required to be a variable that 
	 * holds the current state. If false then the Target value will be used to initialize the state and the Current 
	 * input will be ignored/unnecessary as a state will be maintained by this node.
	 */
	UPROPERTY(meta=(Input, Constant))
	bool bUseCurrentInput;

	/** Current position of the spring. */
	UPROPERTY(meta = (Input, EditCondition = "bUseCurrentInput"))
	float Current;

	/**
	 * The spring strength determines how hard it will pull towards the target. The value is the frequency
	 * at which it will oscillate when there is no damping.
	 */
	UPROPERTY(meta=(Input, ClampMin="0"))
	float Strength;

	/** 
	 * The amount of damping in the spring.
	 * Set it smaller than 1 to make the spring oscillate before stabilizing on the target. 
	 * Set it equal to 1 to reach the target without overshooting. 
	 * Set it higher than one to make the spring take longer to reach the target.
	 */
	UPROPERTY(meta=(Input, ClampMin="0"))
	float CriticalDamping;

	/**
	 * The amount that the velocity should be passed through to the spring. A value of 1 will result in more
	 * responsive output, but if the input is noisy or has step changes, these discontinuities will be passed 
	 * through to the output much more than if a smaller value such as 0 is used.
	 */
	UPROPERTY(meta = (Input, ClampMin="0", ClampMax="1"))
	float TargetVelocityAmount;

	/**
	 * If true, then the initial value will be taken from the target value, and not from the current value.
	 */
	UPROPERTY(meta = (Input, Constant, EditCondition = "bUseCurrentInput"))
	bool bInitializeFromTarget;

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
		bUseCurrentInput = true;
		Current = Target = Result = FVector::ZeroVector;
		Strength = 4.0f;
		CriticalDamping = 1.0f;
		TargetVelocityAmount = 0.0f;
		bInitializeFromTarget = false;
		SpringState = FVectorSpringState();
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/** Rest/target position of the spring. */
	UPROPERTY(meta=(Input))
	FVector Target;

	/**
	 * If true, then the Current input will be used to initialize the state, and is required to be a variable that 
	 * holds the current state. If false then the Target value will be used to initialize the state and the Current 
	 * input will be ignored/unnecessary as a state will be maintained by this node.
	 */
	UPROPERTY(meta=(Input, Constant))
	bool bUseCurrentInput;

	/** Current position of the spring. */
	UPROPERTY(meta = (Input, EditCondition = "bUseCurrentInput"))
	FVector Current;

	/**
	 * The spring strength determines how hard it will pull towards the target. The value is the frequency
	 * at which it will oscillate when there is no damping.
	 */
	UPROPERTY(meta=(Input, ClampMin="0"))
	float Strength;

	/** 
	 * The amount of damping in the spring.
	 * Set it smaller than 1 to make the spring oscillate before stabilizing on the target. 
	 * Set it equal to 1 to reach the target without overshooting. 
	 * Set it higher than one to make the spring take longer to reach the target.
	 */
	UPROPERTY(meta=(Input, ClampMin="0"))
	float CriticalDamping;

	/**
	 * The amount that the velocity should be passed through to the spring. A value of 1 will result in more
	 * responsive output, but if the input is noisy or has step changes, these discontinuities will be passed 
	 * through to the output much more than if a smaller value such as 0 is used.
	 */
	UPROPERTY(meta = (Input, ClampMin="0", ClampMax="1"))
	float TargetVelocityAmount;

	/**
	 * If true, then the initial value will be taken from the target value, and not from the current value. 
	 */
	UPROPERTY(meta = (Input, Constant, EditCondition = "bUseCurrentInput"))
	bool bInitializeFromTarget;

	/** New position of the spring after delta time. */
	UPROPERTY(meta=(Output))
	FVector Result;

	UPROPERTY()
	FVectorSpringState SpringState;
};

/**
 * Uses a simple spring model to interpolate a quaternion from Current to Target.
 */
USTRUCT(meta=(DisplayName="Spring Interpolate", Keywords="Alpha,SpringInterpolate", PrototypeName = "SpringInterp", MenuDescSuffix = "(Quaternion)"))
struct CONTROLRIG_API FRigUnit_SpringInterpQuaternion : public FRigUnit_SimBase
{
	GENERATED_BODY()
	
	FRigUnit_SpringInterpQuaternion()
	{
		bUseCurrentInput = true;
		Current = Target = Result = FQuat::Identity;
		Strength = 4.0f;
		CriticalDamping = 1.0f;
		TargetVelocityAmount = 0.0f;
		bInitializeFromTarget = false;
		SpringState = FQuaternionSpringState();
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/** Rest/target position of the spring. */
	UPROPERTY(meta=(Input))
	FQuat Target;

	/**
	 * If true, then the Current input will be used to initialize the state, and is required to be a variable that 
	 * holds the current state. If false then the Target value will be used to initialize the state and the Current 
	 * input will be ignored/unnecessary as a state will be maintained by this node.
	 */
	UPROPERTY(meta=(Input, Constant))
	bool bUseCurrentInput;

	/** Current position of the spring. */
	UPROPERTY(meta = (Input, EditCondition = "bUseCurrentInput"))
	FQuat Current;

	/**
	 * The spring strength determines how hard it will pull towards the target. The value is the frequency
	 * at which it will oscillate when there is no damping.
	 */
	UPROPERTY(meta=(Input, ClampMin="0"))
	float Strength;

	/** 
	 * The amount of damping in the spring.
	 * Set it smaller than 1 to make the spring oscillate before stabilizing on the target. 
	 * Set it equal to 1 to reach the target without overshooting. 
	 * Set it higher than one to make the spring take longer to reach the target.
	 */
	UPROPERTY(meta=(Input, ClampMin="0"))
	float CriticalDamping;

	/**
	 * The amount that the velocity should be passed through to the spring. A value of 1 will result in more
	 * responsive output, but if the input is noisy or has step changes, these discontinuities will be passed 
	 * through to the output much more than if a smaller value such as 0 is used.
	 */
	UPROPERTY(meta=(Input, ClampMin="0", ClampMax="1"))
	float TargetVelocityAmount;

	/**
	 * If true, then the initial value will be taken from the target value, and not from the current value. 
	 */
	UPROPERTY(meta = (Input, Constant, EditCondition = "bUseCurrentInput"))
	bool bInitializeFromTarget;

	/** New position of the spring after delta time. */
	UPROPERTY(meta=(Output))
	FQuat Result;

	UPROPERTY()
	FQuaternionSpringState SpringState;
};

