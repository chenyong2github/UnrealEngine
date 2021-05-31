// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Curves/RichCurve.h"
#include "Engine/EngineTypes.h"
#include "LensData.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "DistortionParametersTable.generated.h"


/**
 * Distortion parameters associated to a zoom value
 */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FDistortionZoomPoint
{
	GENERATED_BODY()

public:

	/** Input zoom value for this point */
	UPROPERTY()
	float Zoom = 0.0f;

	/** Distortion parameters for this point */
	UPROPERTY(EditAnywhere, Category = "Distortion")
	FDistortionInfo DistortionInfo;
};

/**
 * Contains list of distortion parameters points associated to zoom value
 */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FDistortionFocusPoint
{
	GENERATED_BODY()

public:

	/** Returns number of zoom points */
	int32 GetNumPoints() const;

	/** Returns zoom value for a given index */
	float GetZoom(int32 Index) const;

	/** Adds a new point at InZoom. Updates existing one if tolerance is met */
	bool AddPoint(float InZoom, const FDistortionInfo& InData, float InputTolerance, bool bIsCalibrationPoint);
	
	/** Removes a point corresponding to specified zoom */
	void RemovePoint(float InZoomValue);

	/** Returns true if this point is empty */
	bool IsEmpty() const;
	
	void SetParameterValue(int32 InZoomIndex, float InZoomValue, int32 InParameterIndex, float InParameterValue);

public:

	/** Input focus value for this point */
	UPROPERTY()
	float Focus = 0.0f;

	/** Curves describing desired blending between resulting displacement maps */
	UPROPERTY()
	FRichCurve MapBlendingCurve;

	/** List of zoom points */
	UPROPERTY()
	TArray<FDistortionZoomPoint> ZoomPoints;
};

/**
 * Distortion table containing list of points for each focus and zoom input
 */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FDistortionTable
{
	GENERATED_BODY()

public:

	/** 
	 * Fills OutCurve with all points contained in the given focus 
	 * Returns false if FocusIdentifier is not found or ParameterIndex isn't valid
	 */
	bool BuildParameterCurve(float InFocus, int32 ParameterIndex, FRichCurve& OutCurve) const;

	/** Returns const point for a given focus */
	const FDistortionFocusPoint* GetFocusPoint(float InFocus) const;
	
	/** Returns point for a given focus */
	FDistortionFocusPoint* GetFocusPoint(float InFocus);

	/** Returns all focus points */
	TConstArrayView<FDistortionFocusPoint> GetFocusPoints() const;

	/** Returns all focus points */
	TArray<FDistortionFocusPoint>& GetFocusPoints();

	/** Removes a focus point */
	void RemoveFocusPoint(float InFocus);

	/** Removes a zoom point from a focus point*/
	void RemoveZoomPoint(float InFocus, float InZoom);

	/** Adds a new point in the table */
	bool AddPoint(float InFocus, float InZoom, const FDistortionInfo& InData, float InputTolerance, bool bIsCalibrationPoint);

public:

	/** Lists of focus points */
	UPROPERTY()
	TArray<FDistortionFocusPoint> FocusPoints;
};

