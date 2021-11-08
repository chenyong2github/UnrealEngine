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
	/** The curve will pass through the control points */
	Hermite,

	/** MAX - invalid */
	Max UMETA(Hidden),
};

USTRUCT()
struct CONTROLRIGSPLINE_API FControlRigSplineImpl
{
	GENERATED_BODY()

	FControlRigSplineImpl()	
	{
		SplineMode = ESplineType::BSpline;
		SamplesPerSegment = 16;
	}

	// Spline type
	ESplineType SplineMode;

	// The control points to construct the spline
	TArray<FVector> ControlPoints;

	// The initial lengths between samples
	TArray<float> InitialLengths;

	// The actual spline
	tinyspline::BSpline Spline;

	// Samples per segment, where segment is the portion between two control points
	int32 SamplesPerSegment;

	// The allowed length compression (1.f being do not allow compression`). If 0, no restriction wil be applied.
	float Compression;

	// The allowed length stretch (1.f being do not allow stretch`). If 0, no restriction wil be applied.
	float Stretch;

	// Positions along the "real" curve (no samples in the first and last segments of a hermite spline)
	TArray<FVector> SamplesArray;

	// Accumulated length along the spline given by samples
	TArray<float> AccumulatedLenth;
};

USTRUCT(BlueprintType)
struct CONTROLRIGSPLINE_API FControlRigSpline 
{
	GENERATED_BODY()

	FControlRigSpline()	{}

	virtual ~FControlRigSpline() {}

	FControlRigSpline(const FControlRigSpline& InOther);
	FControlRigSpline& operator =(const FControlRigSpline& InOther);

	TSharedPtr<FControlRigSplineImpl> SplineData;

	/**
	* Sets the control points in the spline. It will build the spline if needed, or forceRebuild is true,
	* or will update the points if building from scratch is not necessary. The type of spline to build will
	* depend on what is set in SplineMode.
	*
	* @param InPoints	The control points to set.
	* @param SplineMode	The type of spline
	* @param SamplesPerSegment The samples to cache for every segment defined between two control rig
	* @param Compression The allowed length compression (1.f being do not allow compression`). If 0, no restriction wil be applied.
	* @param Stretch The allowed length stretch (1.f being do not allow stretch). If 0, no restriction wil be applied.
	*/
	void SetControlPoints(const TArrayView<const FVector>& InPoints, const ESplineType SplineMode = ESplineType::BSpline, const int32 SamplesPerSegment = 16, const float Compression = 1.f, const float Stretch = 1.f);

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
};
