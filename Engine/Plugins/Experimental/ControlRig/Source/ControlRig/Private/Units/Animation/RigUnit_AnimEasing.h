// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Animation/RigUnit_AnimBase.h"
#include "Math/ControlRigMathLibrary.h"
#include "RigUnit_AnimEasing.generated.h"

/**
 * Returns the eased version of the input value
 */
USTRUCT(meta=(DisplayName="Ease", Keywords="Easing,Profile,Smooth,Cubic"))
struct FRigUnit_AnimEasing : public FRigUnit_AnimBase
{
	GENERATED_BODY()
	
	FRigUnit_AnimEasing()
	{
		Value = Result = 0.f;
		Type = EControlRigAnimEasingType::CubicInOut;
		SourceMinimum = TargetMinimum = 0.f;
		SourceMaximum = TargetMaximum = 1.f;
	}

	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta=(Input))
	float Value;

	UPROPERTY(meta=(Input))
	EControlRigAnimEasingType Type;

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

