// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"
#include "IndexTypes.h"
#include "Templates/PimplPtr.h"

template <typename RealType> struct TConvexHull3Internal;

/**
 * Calculate the Convex Hull of a 3D point set as a Triangle Mesh
 */
template<typename RealType>
class TConvexHull3
{
public:

	/**
	 * Calculate the Convex Hull for the given point set.
	 * @param bUseExactComputation If true, high-precision Rational number types are used for the calculation, rather than doubles. This is slower but more reliable.
	 * @return true if convex hull was found
	 */
	bool Solve(int32 NumPoints, TFunctionRef<FVector3<RealType>(int32)> GetPointFunc, bool bUseExactComputation = true);

	/** @return true if convex hull is available */
	bool IsSolutionAvailable() const;

	/**
	 * Call TriangleFunc for each triangle of the Convex Hull. The triangles index into the point set passed to Solve()
	 */
	void GetTriangles(TFunctionRef<void(FIndex3i)> TriangleFunc);


protected:
	void Initialize(int32 NumPoints, bool bUseExactComputation);

	TPimplPtr<TConvexHull3Internal<RealType>> Internal;
};

typedef TConvexHull3<float> FConvexHull3f;
typedef TConvexHull3<double> FConvexHull3d;