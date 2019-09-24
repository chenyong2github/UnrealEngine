// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Debug/RigUnit_DebugBase.h"
#include "RigUnit_DebugLineStrip.generated.h"

USTRUCT(meta=(DisplayName="Draw Line Strip"))
struct FRigUnit_DebugLineStrip : public FRigUnit_DebugBaseMutable
{
	GENERATED_BODY()

	FRigUnit_DebugLineStrip()
	{
		Color = FLinearColor::Red;
		Thickness = 0.f;
		WorldOffset = FTransform::Identity;
		bEnabled = true;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	TArray<FVector> Points;

	UPROPERTY(meta = (Input))
	FLinearColor Color;

	UPROPERTY(meta = (Input))
	float Thickness;

	UPROPERTY(meta = (Input, Constant, BoneName))
	FName Space;

	UPROPERTY(meta = (Input))
	FTransform WorldOffset;
	
	UPROPERTY(meta = (Input, Constant, BoneName))
	bool bEnabled;
};
