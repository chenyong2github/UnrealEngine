// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigUnit_AnimBase.h"
#include "Math/ControlRigMathLibrary.h"
#include "RigUnit_AnimEasing.generated.h"

/**
 * A constant value of an easing type
 */
USTRUCT(meta = (DisplayName = "EaseType", Keywords = "Constant"))
struct CONTROLRIG_API FRigUnit_AnimEasingType : public FRigUnit_AnimBase
{
	GENERATED_BODY()

	FRigUnit_AnimEasingType()
	{
		Type = ERigVMAnimEasingType::CubicEaseInOut;
	}

	RIGVM_METHOD()
	virtual void Execute() override;

	UPROPERTY(meta = (Input, Output))
	ERigVMAnimEasingType Type;
};

/**
 * Returns the eased version of the input value
 */
USTRUCT(meta=(DisplayName="Ease", Keywords="Easing,Profile,Smooth,Cubic"))
struct CONTROLRIG_API FRigUnit_AnimEasing : public FRigUnit_AnimBase
{
	GENERATED_BODY()
	
	FRigUnit_AnimEasing()
	{
		Value = Result = 0.f;
		Type = ERigVMAnimEasingType::CubicEaseInOut;
		SourceMinimum = TargetMinimum = 0.f;
		SourceMaximum = TargetMaximum = 1.f;
	}

	RIGVM_METHOD()
	virtual void Execute() override;

	UPROPERTY(meta=(Input))
	float Value;

	UPROPERTY(meta=(Input))
	ERigVMAnimEasingType Type;

	UPROPERTY(meta=(Input))
	float SourceMinimum;

	UPROPERTY(meta=(Input))
	float SourceMaximum;

	UPROPERTY(meta=(Input))
	float TargetMinimum;

	UPROPERTY(meta=(Input))
	float TargetMaximum;

	UPROPERTY(meta=(Output))
	float Result;
};

