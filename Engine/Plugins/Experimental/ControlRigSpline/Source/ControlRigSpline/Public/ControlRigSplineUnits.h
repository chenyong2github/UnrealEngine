// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "ControlRigDefines.h"
#include "Units/RigUnit.h"
#include "ControlRig.h"
#include "ControlRigSplineTypes.h"
#include "ControlRigSplineUnits.generated.h"

USTRUCT(meta = (Abstract, NodeColor = "0.3 0.1 0.1"))
struct CONTROLRIGSPLINE_API FRigUnit_ControlRigSplineBase : public FRigUnit
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Control Rig Spline From Points", Category = "Control Rig"))
struct CONTROLRIGSPLINE_API FRigUnit_ControlRigSplineFromPoints : public FRigUnit_ControlRigSplineBase
{
	GENERATED_BODY()

	FRigUnit_ControlRigSplineFromPoints()
	{
		SplineMode = ESplineType::BSpline;
	}

	/** Execute logic for this rig unit */
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	TArray<FVector> Points;

	UPROPERTY(meta = (Output))
	FControlRigSpline Spline;

	UPROPERTY(meta = (Input))
	ESplineType SplineMode;
};

USTRUCT(meta = (DisplayName = "Position From Control Rig Spline", Category = "Control Rig"))
struct CONTROLRIGSPLINE_API FRigUnit_PositionFromControlRigSpline : public FRigUnit_ControlRigSplineBase
{
	GENERATED_BODY()

	FRigUnit_PositionFromControlRigSpline()
	{
		U = 0.f;
		Position = FVector::ZeroVector;
	}

	/** Execute logic for this rig unit */
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FControlRigSpline Spline;

	UPROPERTY(meta = (Input))
	float U;

	UPROPERTY(meta = (Output))
	FVector Position;
};

USTRUCT(meta = (DisplayName = "Transform From Control Rig Spline", Category = "Control Rig"))
struct CONTROLRIGSPLINE_API FRigUnit_TransformFromControlRigSpline : public FRigUnit_ControlRigSplineBase
{
	GENERATED_BODY()

	FRigUnit_TransformFromControlRigSpline()
	{
		UpVector = FVector::UpVector;
		Twist = 0.f;
		U = 0.f;
		Transform = FTransform::Identity;
	}

	/** Execute logic for this rig unit */
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FControlRigSpline Spline;

	UPROPERTY(meta = (Input))
	FVector UpVector;

	UPROPERTY(meta = (Input))
	float Twist;

	UPROPERTY(meta = (Input))
	float U;

	UPROPERTY(meta = (Output))
	FTransform Transform;
};

USTRUCT(meta = (DisplayName = "Draw Control Rig Spline", Category = "Control Rig"))
struct CONTROLRIGSPLINE_API FRigUnit_DrawControlRigSpline : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_DrawControlRigSpline()
	{
		Color = FLinearColor::Red;
		Thickness = 1.f;
		Detail = 16.f;
	}

	/** Execute logic for this rig unit */
	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(meta = (Input))
	FControlRigSpline Spline;

	UPROPERTY(meta = (Input))
	FLinearColor Color;

	UPROPERTY(meta = (Input))
	float Thickness;

	UPROPERTY(meta = (Input))
	int32 Detail;
};

