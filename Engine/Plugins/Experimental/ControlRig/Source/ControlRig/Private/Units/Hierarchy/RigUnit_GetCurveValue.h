// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_GetCurveValue.generated.h"

/**
 * GetCurveValue is used to retrieve a single transform from a Curve.
 */
USTRUCT(meta=(DisplayName="Get Curve Value", Category="Curve", Keywords="GetCurveValue"))
struct FRigUnit_GetCurveValue : public FRigUnit
{
	GENERATED_BODY()

	FRigUnit_GetCurveValue()
		: Curve(NAME_None)
		, Value(0.f)
		, CachedCurveIndex(INDEX_NONE)
	{}

	virtual FString GetUnitLabel() const override;
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Curve to retrieve the transform for.
	 */
	UPROPERTY(meta = (Input, CurveName, Constant))
	FName Curve;

	// The current transform of the given Curve - or identity in case it wasn't found.
	UPROPERTY(meta=(Output, UIMin = 0.f, UIMax = 1.f))
	float Value;

private:
	// Used to cache the internally used Curve index
	UPROPERTY()
	int32 CachedCurveIndex;
};
