// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"
#include "IndexTypes.h"
#include "Polygon2.h"
#include "Templates/PimplPtr.h"

template <typename RealType> struct TConvexHull2Internal;

/**
 * Calculate the Convex Hull of a 2D point set as a Polygon
 */
template<typename RealType>
class TConvexHull2
{
public:

	/**
	 * Calculate the Convex Hull for the given point set.
	 * @param bUseExactComputation If true, high-precision Rational number types are used for the calculation, rather than doubles. This is slower but more reliable.
	 * @return true if convex hull was found
	 */
	bool Solve(int32 NumPoints, TFunctionRef<FVector2<RealType>(int32)> GetPointFunc, bool bUseExactComputation = true);

	/** @return true if convex hull is available */
	bool IsSolutionAvailable() const;

	/**
	 * @return the calculated polygon in PolygonOut
	 */
	void GetPolygon(TPolygon2<RealType>& PolygonOut) const;


protected:
	void Initialize(int32 NumPoints, bool bUseExactComputation);

	TPimplPtr<TConvexHull2Internal<RealType>> Internal;
};

typedef TConvexHull2<float> FConvexHull2f;
typedef TConvexHull2<double> FConvexHull2d;