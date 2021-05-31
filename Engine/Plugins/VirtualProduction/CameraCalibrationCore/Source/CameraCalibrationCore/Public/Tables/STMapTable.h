// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Curves/RichCurve.h"
#include "Engine/EngineTypes.h"
#include "LensData.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"

#include "STMapTable.generated.h"



/**
 * Derived data computed from parameters or stmap
 */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FDerivedDistortionData
{
	GENERATED_BODY()

	/** Precomputed data about distortion */
	UPROPERTY(VisibleAnywhere, Category = "Distortion")
	FDistortionData DistortionData;

	/** Computed displacement map based on undistortion data */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Distortion")
	UTextureRenderTarget2D* UndistortionDisplacementMap = nullptr;

	/** Computed displacement map based on distortion data */
	UPROPERTY(Transient, VisibleAnywhere, Category = "Distortion")
	UTextureRenderTarget2D* DistortionDisplacementMap = nullptr;

	/** When dirty, derived data needs to be recomputed */
	bool bIsDirty = true;
};

/**
 * STMap data associated to a zoom input value
 */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FSTMapZoomPoint
{
	GENERATED_BODY()

public:

	/** Input zoom value for this point */
	UPROPERTY()
	float Zoom = 0.0f;

	/** Data for this zoom point */
	UPROPERTY()
	FSTMapInfo STMapInfo;

	/** Derived distortion data associated with this point */
	UPROPERTY(Transient)
	FDerivedDistortionData DerivedDistortionData;

	/** Whether this point was added in calibration along distortion */
	UPROPERTY()
	bool bIsCalibrationPoint = false;
};

/**
 * A data point associating focus and zoom to lens parameters
 */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FSTMapFocusPoint
{
	GENERATED_BODY()

public:

	/** Returns number of zoom points */
	int32 GetNumPoints() const;

	/** Returns zoom value for a given index */
	float GetZoom(int32 Index) const;
	
	/** Returns const point for a given zoom */
	const FSTMapZoomPoint* GetZoomPoint(float InZoom) const;

	/** Returns point for a given focus */
	FSTMapZoomPoint* GetZoomPoint(float InZoom);

	/** Adds a new point at InZoom. Updates existing one if tolerance is met */
	bool AddPoint(float InZoom, const FSTMapInfo& InData, float InputTolerance, bool bIsCalibrationPoint);
	
	/** Removes a point corresponding to specified zoom */
	void RemovePoint(float InZoomValue);

	/** Returns true if this point is empty */
	bool IsEmpty() const;
	
public:

	/** Input focus for this point */
	UPROPERTY()
	float Focus = 0.0f;

	/** Curve used to blend displacement map together to give user more flexibility */
	UPROPERTY()
	FRichCurve MapBlendingCurve;

	/** Zoom points for this focus */
	UPROPERTY()
	TArray<FSTMapZoomPoint> ZoomPoints;
};

/**
 * STMap table containing list of points for each focus and zoom inputs
 */
USTRUCT()
struct CAMERACALIBRATIONCORE_API FSTMapTable
{
	GENERATED_BODY()

public:

	/** 
	 * Builds the map blending curve into OutCurve
	 * Returns true if focus point exists
	 */
	bool BuildMapBlendingCurve(float InFocus, FRichCurve& OutCurve);
	
	/** Returns const point for a given focus */
	const FSTMapFocusPoint* GetFocusPoint(float InFocus) const;

	/** Returns point for a given focus */
	FSTMapFocusPoint* GetFocusPoint(float InFocus);

	/** Returns all focus points */
	TConstArrayView<FSTMapFocusPoint> GetFocusPoints() const;

	/** Returns all focus points */
	TArrayView<FSTMapFocusPoint> GetFocusPoints();

	/** Removes a focus point identified as InFocusIdentifier */
	void RemoveFocusPoint(float InFocus);

	/** Removes a focus point identified as InFocusIdentifier */
	void RemoveZoomPoint(float InFocus, float InZoom);

	/** Adds a new point in the table */
	bool AddPoint(float InFocus, float InZoom, const FSTMapInfo& InData,  float InputTolerance, bool bIsCalibrationPoint);
	

public:

	/** Lists of focus points */
	UPROPERTY()
	TArray<FSTMapFocusPoint> FocusPoints;
};

