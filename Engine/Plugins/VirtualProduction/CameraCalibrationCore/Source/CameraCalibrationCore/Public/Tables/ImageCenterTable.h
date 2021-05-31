// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Curves/RichCurve.h"
#include "Engine/EngineTypes.h"
#include "LensData.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "ImageCenterTable.generated.h"


/**
 * ImageCenter focus point containing curves for CxCy 
 */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FImageCenterFocusPoint
{
	GENERATED_BODY()

public:

	/** Returns number of zoom points */
	int32 GetNumPoints() const;

	/** Returns zoom value for a given index */
	float GetZoom(int32 Index) const;

	/** Adds a new point at InZoom. Updates existing one if tolerance is met */
	bool AddPoint(float InZoom, const FImageCenterInfo& InData, float InputTolerance, bool bIsCalibrationPoint);
	
	/** Removes a point corresponding to specified zoom */
	void RemovePoint(float InZoomValue);

	/** Returns true if this point is empty */
	bool IsEmpty() const;

public:

	/** Focus value of this point */
	UPROPERTY()
	float Focus = 0.0f;

	/** Curves representing normalized Cx over zoom */
	UPROPERTY()
	FRichCurve Cx;

	/** Curves representing normalized Cy over zoom */
	UPROPERTY()
	FRichCurve Cy;
};

/**
 * Image Center table associating CxCy values to focus and zoom
 */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FImageCenterTable
{
	GENERATED_BODY()

public:

	/** 
	* Fills OutCurve with all points contained in the given focus 
	* Returns false if FocusIdentifier is not found or ParameterIndex isn't valid
	*/
	bool BuildParameterCurve(float InFocus, int32 ParameterIndex, FRichCurve& OutCurve) const;
	
	/** Returns const point for a given focus */
	const FImageCenterFocusPoint* GetFocusPoint(float InFocus) const;

	/** Returns const point for a given focus */
	FImageCenterFocusPoint* GetFocusPoint(float InFocus);

	/** Returns all focus points */
	TConstArrayView<FImageCenterFocusPoint> GetFocusPoints() const;

	/** Returns all focus points */
	TArray<FImageCenterFocusPoint>& GetFocusPoints();
	
	/** Removes a focus point identified as InFocusIdentifier */
	void RemoveFocusPoint(float InFocus);

	/** Removes a focus point identified as InFocusIdentifier */
	void RemoveZoomPoint(float InFocus, float InZoom);

	/** Adds a new point in the table */
	bool AddPoint(float InFocus, float InZoom, const FImageCenterInfo& InData,  float InputTolerance, bool bIsCalibrationPoint);

public:

	/** Lists of focus points */
	UPROPERTY()
	TArray<FImageCenterFocusPoint> FocusPoints;
};

