// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Math/UnrealMathUtility.h"
#include "Math/Plane.h"
#include "Math/Matrix.h"

// LWC_TODO: FRotator -> TRotator<T>

namespace UE {
namespace Math {
 	
/** Inverse Rotation matrix */
template<typename T>
struct TInverseRotationMatrix
	: public TMatrix<T>
{
public:
	/**
	 * Constructor.
	 *
	 * @param Rot rotation
	 */
	TInverseRotationMatrix(const FRotator& Rot);
};

template<typename T>
FORCEINLINE TInverseRotationMatrix<T>::TInverseRotationMatrix(const FRotator& Rot)
	: TMatrix<T>(
		TMatrix<T>( // Yaw
			TPlane<T>(+FMath::Cos(Rot.Yaw * PI / 180.f), -FMath::Sin(Rot.Yaw * PI / 180.f), 0.0f, 0.0f),
			TPlane<T>(+FMath::Sin(Rot.Yaw * PI / 180.f), +FMath::Cos(Rot.Yaw * PI / 180.f), 0.0f, 0.0f),
			TPlane<T>(0.0f, 0.0f, 1.0f, 0.0f),
			TPlane<T>(0.0f, 0.0f, 0.0f, 1.0f)) *
		TMatrix<T>( // Pitch
			TPlane<T>(+FMath::Cos(Rot.Pitch * PI / 180.f), 0.0f, -FMath::Sin(Rot.Pitch * PI / 180.f), 0.0f),
			TPlane<T>(0.0f, 1.0f, 0.0f, 0.0f),
			TPlane<T>(+FMath::Sin(Rot.Pitch * PI / 180.f), 0.0f, +FMath::Cos(Rot.Pitch * PI / 180.f), 0.0f),
			TPlane<T>(0.0f, 0.0f, 0.0f, 1.0f)) *
		TMatrix<T>( // Roll
			TPlane<T>(1.0f, 0.0f, 0.0f, 0.0f),
			TPlane<T>(0.0f, +FMath::Cos(Rot.Roll * PI / 180.f), +FMath::Sin(Rot.Roll * PI / 180.f), 0.0f),
			TPlane<T>(0.0f, -FMath::Sin(Rot.Roll * PI / 180.f), +FMath::Cos(Rot.Roll * PI / 180.f), 0.0f),
			TPlane<T>(0.0f, 0.0f, 0.0f, 1.0f))
	)
{ }

} // namespace Math
} // namespace UE

DECLARE_LWC_TYPE(InverseRotationMatrix, 44);