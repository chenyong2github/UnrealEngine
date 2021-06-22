// Copyright Epic Games, Inc. All Rights Reserved.


#include "CameraCalibrationUtils.h"

#include "Engine/Texture2D.h"
#include "Math/UnrealMathUtility.h"

#include <type_traits>

namespace CameraCalibrationUtils
{
	using EAxis = FCameraCalibrationUtils::EAxis;

	// These axes must match the order in which they are declared in EAxis
	static const TArray<FVector> UnitVectors =
	{
		{  1,  0,  0 }, //  X
		{  0,  1,  0 }, //  Y
		{  0,  0,  1 }, //  Z
		{ -1,  0,  0 }, // -X
		{  0, -1,  0 }, // -Y
		{  0,  0, -1 }, // -Z
	};

	static const FVector& UnitVectorFromAxisEnum(EAxis Axis)
	{
		return UnitVectors[std::underlying_type_t<EAxis>(Axis)];
	};
}

void FCameraCalibrationUtils::ConvertCoordinateSystem(FTransform& Transform, EAxis SrcXInDstAxis, EAxis SrcYInDstAxis, EAxis SrcZInDstAxis)
{
	// Unreal Engine:
	//   Front : X
	//   Right : Y
	//   Up    : Z
	//
	// OpenCV:
	//   Front : Z
	//   Right : X
	//   Up    : Yn

	using namespace CameraCalibrationUtils;

	FMatrix M12 = FMatrix::Identity;

	M12.SetColumn(0, UnitVectorFromAxisEnum(SrcXInDstAxis));
	M12.SetColumn(1, UnitVectorFromAxisEnum(SrcYInDstAxis));
	M12.SetColumn(2, UnitVectorFromAxisEnum(SrcZInDstAxis));

	Transform.SetFromMatrix(M12.GetTransposed() * Transform.ToMatrixWithScale() * M12);
}

void FCameraCalibrationUtils::ConvertUnrealToOpenCV(FTransform& Transform)
{
	ConvertCoordinateSystem(Transform, EAxis::Y, EAxis::Zn, EAxis::X);
}

void FCameraCalibrationUtils::ConvertOpenCVToUnreal(FTransform& Transform)
{
	ConvertCoordinateSystem(Transform, EAxis::Z, EAxis::X, EAxis::Yn);
}

bool FCameraCalibrationUtils::IsNearlyEqual(const FTransform& A, const FTransform& B, float MaxLocationDelta, float MaxAngleDeltaDegrees)
{
	// Location check

	const float LocationDeltaInCm = (B.GetLocation() - A.GetLocation()).Size();

	if (LocationDeltaInCm > MaxLocationDelta)
	{
		return false;
	}

	// Rotation check

	const float AngularDistanceRadians = FMath::Abs(A.GetRotation().AngularDistance(B.GetRotation()));

	if (AngularDistanceRadians > FMath::DegreesToRadians(MaxAngleDeltaDegrees))
	{
		return false;
	}

	return true;
}

