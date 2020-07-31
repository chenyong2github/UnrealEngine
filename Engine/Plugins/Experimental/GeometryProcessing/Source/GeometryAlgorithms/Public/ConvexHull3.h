// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VectorTypes.h"
#include "IndexTypes.h"
#include "LineTypes.h"
#include "PlaneTypes.h"
#include "Templates/PimplPtr.h"

template <typename RealType> struct TConvexHull3Internal;

/**
 * Calculate the Convex Hull of a 3D point set as a Triangle Mesh
 */
template<typename RealType>
class GEOMETRYALGORITHMS_API TConvexHull3
{
public:

	/**
	 * Generate convex hull as long as input is not degenerate
	 * If input is degenerate, this will return false, and caller can call GetDimension()
	 * to determine whether the points were coplanar, collinear, or all the same point
	 *
	 * @param Points Array of points to consider
	 * @param Filter Optional filter to include only a subset of the points in the output hull
	 * @return true if hull was generated, false if points span < 2 dimensions
	 */
	bool Solve(int32 NumPoints, TFunctionRef<FVector3<RealType>(int32)> GetPointFunc, TFunctionRef<bool(int32)> FilterFunc = [](int32 Idx) {return true;});

	/**
	 * Generate convex hull as long as input is not degenerate
	 * If input is degenerate, this will return false, and caller can call GetDimension()
	 * to determine whether the points were collinear, or all the same point
	 *
	 * @param NumPoints Number of points to consider
	 * @param GetPointFunc Function providing array-style access into points
	 * @param FilterFunc Optional filter to include only a subset of the points in the output hull
	 * @return true if hull was generated, false if points span < 2 dimensions
	 */
	bool Solve(TArrayView<const FVector3<RealType>> Points, TFunctionRef<bool(int32)> FilterFunc)
	{
		return Solve(Points.Num(), [&Points](int32 Idx)
			{
				return Points[Idx];
			}, FilterFunc);
	}

	// default FilterFunc version of the above Solve(); workaround for clang bug https://bugs.llvm.org/show_bug.cgi?id=25333
	/**
	 * Generate convex hull as long as input is not degenerate
	 * If input is degenerate, this will return false, and caller can call GetDimension()
	 * to determine whether the points were collinear, or all the same point
	 *
	 * @param NumPoints Number of points to consider
	 * @param GetPointFunc Function providing array-style access into points
	 * @return true if hull was generated, false if points span < 2 dimensions
	 */
	bool Solve(TArrayView<const FVector3<RealType>> Points)
	{
		return Solve(Points.Num(), [&Points](int32 Idx)
			{
				return Points[Idx];
			}, [](int32 Idx) {return true;});
	}

	/** @return true if convex hull is available */
	bool IsSolutionAvailable() const
	{
		return Dimension == 3;
	}

	/**
	 * Call TriangleFunc for each triangle of the Convex Hull. The triangles index into the point set passed to Solve()
	 */
	void GetTriangles(TFunctionRef<void(FIndex3i)> TriangleFunc)
	{
		for (FIndex3i Triangle : Hull)
		{
			TriangleFunc(Triangle);
		}
	}

	/** @return convex hull triangles */
	TArray<FIndex3i> const& GetTriangles() const
	{
		return Hull;
	}

	/**
	 * Empty any previously-computed convex hull data.  Frees the hull memory.
	 * Note: You do not need to call this before calling Generate() with new data.
	 */
	void Empty()
	{
		Dimension = 0;
		NumUniquePoints = 0;
		Hull.Empty();
	}

	/**
	 * Number of dimensions spanned by the input points.
	 */
	inline int GetDimension() const
	{
		return Dimension;
	}
	/** @return If GetDimension() returns 1, this returns the line the data was on.  Otherwise result is not meaningful. */
	inline TLine3<RealType> const& GetLine() const
	{
		return Line;
	}
	/** @return If GetDimension() returns 2, this returns the plane the data was on.  Otherwise result is not meaningful. */
	inline TPlane3<RealType> const& GetPlane() const
	{
		return Plane;
	}

	/** @return Number of unique points considered by convex hull construction (excludes exact duplicate points and filtered-out points) */
	int GetNumUniquePoints() const
	{
		return NumUniquePoints;
	}

protected:
	// Incrementally insert ith point
	void Insert(TFunctionRef<FVector3<RealType>(int32)> GetPointFunc, int Idx);

	int32 Dimension;
	TLine3<RealType> Line;
	TPlane3<RealType> Plane;

	int NumUniquePoints = 0;
	TArray<FIndex3i> Hull;
	
};

typedef TConvexHull3<float> FConvexHull3f;
typedef TConvexHull3<double> FConvexHull3d;