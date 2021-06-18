// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FTransform;

class UTexture2D;

/**
 * Helper class for commonly used functions for camera calibration.
 */
class CAMERACALIBRATIONCORE_API FCameraCalibrationUtils
{
public:

	/** Enumeration to specify any cartesian axis in positive or negative directions */
	enum class EAxis
	{
		X, Y, Z,
		Xn, Yn, Zn,
	};

public:

	/** Converts in-place the coordinate system of the given FTransform by specifying the source axes in terms of the destionation axes */
	static void ConvertCoordinateSystem(FTransform& Transform, EAxis DstXInSrcAxis, EAxis DstYInSrcAxis, EAxis DstZInSrcAxis);

	/** Converts in-place an FTransform in Unreal coordinates to OpenCV coordinates */
	static void ConvertUnrealToOpenCV(FTransform& Transform);

	/** Converts in-place an FTransform in OpenCV coordinates to Unreal coordinates */
	static void ConvertOpenCVToUnreal(FTransform& Transform);

	/** Compares two transforms and returns true if they are nearly equal in distance and angle */
	static bool IsNearlyEqual(const FTransform& A, const FTransform& B, float MaxLocationDelta = 2.0f, float MaxAngleDegrees = 2.0f);
};
