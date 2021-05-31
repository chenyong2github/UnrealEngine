// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Curves/RichCurve.h"
#include "Engine/EngineTypes.h"
#include "LensData.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "FocalLengthTable.generated.h"


/**
 * Focal length associated to a zoom value
 */
USTRUCT(BlueprintType)
struct CAMERACALIBRATIONCORE_API FFocalLengthZoomPoint
{
	GENERATED_BODY()

public:

	/** Input zoom value for this point */
	UPROPERTY()
	float Zoom = 0.0f;

	/** Value expected to be normalized (unitless) */
	UPROPERTY()
	FFocalLengthInfo FocalLengthInfo;

	/** Whether this focal length was added along calibrated distortion parameters */
	UPROPERTY()
	bool bIsCalibrationPoint = false;
};

/**
 * Contains list of focal length points associated to zoom value
 */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FFocalLengthFocusPoint
{
	GENERATED_BODY()
	
public:

	/** Returns number of zoom points */
	int32 GetNumPoints() const;

	/** Returns zoom value for a given index */
	float GetZoom(int32 Index) const;

	/** Adds a new point at InZoom. Updates existing one if tolerance is met */
	bool AddPoint(float InZoom, const FFocalLengthInfo& InData, float InputTolerance, bool bIsCalibrationPoint);

	/** Returns data at the requested index */
	bool GetValue(int32 Index, FFocalLengthInfo& OutData) const;

	/** Removes a point corresponding to specified zoom */
   	void RemovePoint(float InZoomValue);

	/** Returns true if this point is empty */
	bool IsEmpty() const;

public:

	/** Input focus for this point */
	UPROPERTY()
	float Focus = 0.0f;

	/** Curves mapping normalized Fx value to Zoom value (Time) */
	UPROPERTY()
	FRichCurve Fx;

	/** Curves mapping normalized Fy value to Zoom value (Time) */
	UPROPERTY()
	FRichCurve Fy;

	/** Used to know points that are locked */
	UPROPERTY()
	TArray<FFocalLengthZoomPoint> ZoomPoints;
};

/**
 * Focal Length table containing FxFy values for each focus and zoom input values
 */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FFocalLengthTable
{
	GENERATED_BODY()

public:

	/** 
	* Fills OutCurve with all points contained in the given focus 
	* Returns false if FocusIdentifier is not found or ParameterIndex isn't valid
	*/
	bool BuildParameterCurve(float InFocus, int32 ParameterIndex, FRichCurve& OutCurve) const;
	
	/** Returns const point for a given focus */
	const FFocalLengthFocusPoint* GetFocusPoint(float InFocus) const;
	
	/** Returns point for a given focus */
	FFocalLengthFocusPoint* GetFocusPoint(float InFocus);

	/** Returns all focus points */
	TConstArrayView<FFocalLengthFocusPoint> GetFocusPoints() const;
	
	/** Removes a focus point identified as InFocusIdentifier */
	void RemoveFocusPoint(float InFocus);

	/** Removes a focus point identified as InFocusIdentifier */
	void RemoveZoomPoint(float InFocus, float InZoom);

	/** Adds a new point in the table */
	bool AddPoint(float InFocus, float InZoom, const FFocalLengthInfo& InData,  float InputTolerance, bool bIsCalibrationPoint);


public:

	/** Lists of focus points */
	UPROPERTY()
	TArray<FFocalLengthFocusPoint> FocusPoints;
};

