// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_SetCurveValue.generated.h"


/**
 * SetCurveValue is used to perform a change in the curve container by setting a single Curve value.
 */
USTRUCT(meta=(DisplayName="Set Curve Value", Category="Hierarchy", Keywords = "SetCurveValue"))
struct FRigUnit_SetCurveValue : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetCurveValue()
		: Value(0.f)
		, CachedCurveIndex(INDEX_NONE)
	{}

	virtual FString GetUnitLabel() const override;
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Curve to set the Value for.
	 */
	UPROPERTY(meta = (Input, CurveName, Constant))
	FName Curve;

	/**
	 * The value to set for the given Curve.
	 */
	UPROPERTY(meta = (Input))
	float Value;

private:
	// Used to cache the internally used curve index
	UPROPERTY()
	int32 CachedCurveIndex;
};
