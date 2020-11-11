// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ControlRigDefines.h"
#include "tinysplinecxx.h"
#include "ControlRigSplineTypes.generated.h"

class UControlRig;

UENUM()
enum class ESplineType : uint8
{
	/** BSpline */
	/** The smooth curve will pass through the first and last control points */
	BSpline,

	/** Hermite: */
	/** The curve will pass through the control points (except the first and last points, which are used as tangents) */
	Hermite,

	/** MAX - invalid */
	Max UMETA(Hidden),
};

USTRUCT(BlueprintType)
struct CONTROLRIGSPLINE_API FControlRigSpline 
{
	GENERATED_BODY()

	FControlRigSpline()	{}
	virtual ~FControlRigSpline() {}

	// Spline type
	ESplineType SplineMode;

	TSharedPtr<tinyspline::BSpline> BSpline;

	/**
	* Sets the control points in the spline. It will build the spline if needed, or forceRebuild is true,
	* or will update the points if building from scratch is not necessary. The type of spline to build will
	* depend on what is set in SplineMode.
	*
	* @param InPoints	The control points to set.
	* @param forceRebuild	If true, will build the spline from scratch.
	*/
	void SetControlPoints(const TArray<FVector>& InPoints, const bool forceRebuild = false);

	/**
	* Populates OutPoints and returns the number of control points in the spline.
	*
	* @param OutPoints	The array to populate.
	* @return			The number of control points.
	*/
	int32 GetControlPoints(TArray<FVector>& OutPoints) const;

	/**
	* Given an InParam float in [0, 1], will return the position of the spline at that point.
	*
	* @param InParam	The parameter between [0, 1] to query.
	* @return			The position in the spline.
	*/
	FVector PositionAtParam(const float InParam) const;

	/**
	* Given an InParam float in [0, 1], will return the tangent vector of the spline at that point. 
	* Note that this vector is not normalized.
	*
	* @param InParam	The parameter between [0, 1] to query.
	* @return			The tangent of the spline at InParam.
	*/
	FVector TangentAtParam(const float InParam) const;

	// Auxiliary control points array, which is different from the Points array
	// when SplineMode is Hermite
	TArray<float> ControlPointsArray;
};
