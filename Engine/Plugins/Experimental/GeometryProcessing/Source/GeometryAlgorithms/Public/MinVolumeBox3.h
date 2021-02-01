// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"
#include "OrientedBoxTypes.h"
#include "Templates/PimplPtr.h"

template <typename RealType> struct TMinVolumeBox3Internal;

class FProgressCancel;

/**
 * Calculate a Minimal-Volume Oriented Box for a set of 3D points.
 * This internally first computes the Convex Hull of the point set. 
 * The minimal box is then guaranteed to be aligned with one of the faces of the convex hull.
 * Note that this is increasingly expensive as the Convex Hull face count increases.
 */
template<typename RealType>
class TMinVolumeBox3
{
public:
	/**
	 * Calculate the minimal box for the given point set.
	 * @param bUseExactBox If true, high-precision number types are used for the minimal-box calculation, rather than doubles. This is *much* slower but more accurate (but not recommended).
	 * @return true if minimal box was found
	 */
	bool Solve(int32 NumPoints, TFunctionRef<FVector3<RealType>(int32)> GetPointFunc, bool bUseExactBox = false, FProgressCancel* Progress = nullptr);

	/** @return true if minimal box is available */
	bool IsSolutionAvailable() const;

	/** @return minimal box in BoxOut */
	void GetResult(TOrientedBox3<RealType>& BoxOut);

protected:
	void Initialize(int32 NumPoints, bool bUseExactBox);

	TPimplPtr<TMinVolumeBox3Internal<RealType>> Internal;
};

typedef TMinVolumeBox3<float> FMinVolumeBox3f;
typedef TMinVolumeBox3<double> FMinVolumeBox3d;