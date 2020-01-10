// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Debug/RigUnit_DebugBase.h"
#include "Math/ControlRigMathLibrary.h"
#include "RigUnit_DebugBezier.generated.h"

USTRUCT(meta=(DisplayName="Draw Bezier"))
struct FRigUnit_DebugBezier : public FRigUnit_DebugBaseMutable
{
	GENERATED_BODY()

	FRigUnit_DebugBezier()
	{
		Bezier = FCRFourPointBezier();
		Color = FLinearColor::Red;
		MinimumU = 0.f;
		MaximumU = 1.f;
		Thickness = 0.f;
		Detail = 16.f;
		WorldOffset = FTransform::Identity;
		bEnabled = true;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FCRFourPointBezier Bezier;

	UPROPERTY(meta = (Input))
	float MinimumU;

	UPROPERTY(meta = (Input))
	float MaximumU;

	UPROPERTY(meta = (Input))
	FLinearColor Color;

	UPROPERTY(meta = (Input))
	float Thickness;

	UPROPERTY(meta = (Input))
	int32 Detail;

	UPROPERTY(meta = (Input, Constant, BoneName))
	FName Space;

	UPROPERTY(meta = (Input))
	FTransform WorldOffset;

	UPROPERTY(meta = (Input, Constant, BoneName))
	bool bEnabled;
};
