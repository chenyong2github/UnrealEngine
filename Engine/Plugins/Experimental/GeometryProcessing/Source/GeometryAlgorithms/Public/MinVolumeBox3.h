// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"
#include "OrientedBoxTypes.h"
#include "Templates/PimplPtr.h"

template <typename RealType> struct TMinVolumeBox3Internal;

/**
 * Calculate a Minimal-Volume Oriented Box for a set of 3D points.
 * This internally first computes the Convex Hull of the point set. 
 * The minimal box is then guaranteed to be aligned with one of the faces of the convex hull.
 * Note that this is relatively expensive if the Convex Hull has a large number of faces.
 */
template<typename RealType>
class TMinVolumeBox3
{
public:
	/**
	 * Calculate the minimal box for the given point set.
	 * @param bUseExactComputation If true, high-precision Rational number types are used for the calculation, rather than doubles. This is slower but more reliable.
	 * @return true if minimal box was found
	 */
	bool Solve(int32 NumPoints, TFunctionRef<FVector3<RealType>(int32)> GetPointFunc, bool bUseExactComputation = true);

	/** @return true if minimal box is available */
	bool IsSolutionAvailable() const;

	/** @return minimal box in BoxOut */
	void GetResult(TOrientedBox3<RealType>& BoxOut);

protected:
	void Initialize(int32 NumPoints, bool bUseExactComputation);

	TPimplPtr<TMinVolumeBox3Internal<RealType>> Internal;
};

typedef TMinVolumeBox3<float> FMinVolumeBox3f;
typedef TMinVolumeBox3<double> FMinVolumeBox3d;