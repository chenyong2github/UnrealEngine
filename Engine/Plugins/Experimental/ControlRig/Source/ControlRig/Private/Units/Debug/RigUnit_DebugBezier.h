// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Debug/RigUnit_DebugBase.h"
#include "RigUnit_DebugBezier.generated.h"

USTRUCT(meta=(DisplayName="Draw Bezier"))
struct FRigUnit_DebugBezier : public FRigUnit_DebugBaseMutable
{
	GENERATED_BODY()

	FRigUnit_DebugBezier()
	{
		A = B = C = D = FVector::ZeroVector;
		Color = FLinearColor::Red;
		MinimumU = 0.f;
		MaximumU = 1.f;
		Thickness = 0.f;
		Detail = 16.f;
		WorldOffset = FTransform::Identity;
		bEnabled = true;
	}

	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FVector A;

	UPROPERTY(meta = (Input))
	FVector B;

	UPROPERTY(meta = (Input))
	FVector C;

	UPROPERTY(meta = (Input))
	FVector D;

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
