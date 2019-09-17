// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Debug/RigUnit_DebugBase.h"
#include "Drawing/ControlRigDrawInterface.h"
#include "RigUnit_DebugHierarchy.generated.h"

USTRUCT(meta=(DisplayName="Draw Hierarchy"))
struct FRigUnit_DebugHierarchy : public FRigUnit_DebugBaseMutable
{
	GENERATED_BODY()

	FRigUnit_DebugHierarchy()
	{
		Mode = EControlRigDrawHierarchyMode::Axes;
		Scale = 10.f;
		Color = FLinearColor::White;
		Thickness = 0.f;
		WorldOffset = FTransform::Identity;
		bEnabled = true;
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	TEnumAsByte<EControlRigDrawHierarchyMode::Type> Mode;

	UPROPERTY(meta = (Input))
	float Scale;

	UPROPERTY(meta = (Input))
	FLinearColor Color;

	UPROPERTY(meta = (Input))
	float Thickness;

	UPROPERTY(meta = (Input))
	FTransform WorldOffset;

	UPROPERTY(meta = (Input, Constant, BoneName))
	bool bEnabled;
};
