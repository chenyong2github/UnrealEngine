// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of WildMagic DistPoint3TTriangle3

#pragma once

#include "VectorTypes.h"
#include "VectorUtil.h"

/**
 * Triangle utility functions
 */
namespace TriangleUtil
{
	/**
	 * @return the edge length of an equilateral/regular triangle with the given area
	 */
	template<typename RealType>
	RealType EquilateralEdgeLengthForArea(RealType TriArea)
	{
		return TMathUtil<RealType>::Sqrt(((RealType)4 * TriArea) / TMathUtil<RealType>::Sqrt3);
	}

};




template<typename RealType>
struct TTriangle2
{
	FVector2<RealType> V[3];

	TTriangle2() {}

	TTriangle2(const FVector2<RealType>& V0, const FVector2<RealType>& V1, const FVector2<RealType>& V2)
	{
		V[0] = V0;
		V[1] = V1;
		V[2] = V2;
	}

	TTriangle2(const FVector2<RealType> VIn[3])
	{
		V[0] = VIn[0];
		V[1] = VIn[1];
		V[2] = VIn[2];
	}

	FVector2<RealType> BarycentricPoint(RealType Bary0, RealType Bary1, RealType Bary2) const
	{
		return Bary0 * V[0] + Bary1 * V[1] + Bary2 * V[2];
	}

	FVector2<RealType> BarycentricPoint(const FVector3<RealType>& BaryCoords) const
	{
		return BaryCoords[0] * V[0] + BaryCoords[1] * V[1] + BaryCoords[2] * V[2];
	}


	/**
	 * @param A first vertex of triangle
	 * @param B second vertex of triangle
	 * @param C third vertex of triangle
	 * @return signed area of triangle
	 */
	static RealType SignedArea(const FVector2<RealType>& A, const FVector2<RealType>& B, const FVector2<RealType>& C)
	{
		return ((RealType)0.5) * ((A.X*B.Y - A.Y*B.X) + (B.X*C.Y - B.Y*C.X) + (C.X*A.Y - C.Y*A.X));
	}

	/** @return signed area of triangle */
	RealType SignedArea() const
	{
		return SignedArea(V[0], V[1], V[2]);
	}


	/**
	 * @param A first vertex of triangle
	 * @param B second vertex of triangle
	 * @param C third vertex of triangle
	 * @param QueryPoint test point
	 * @return true if QueryPoint is inside triangle
	 */
	static bool IsInside(const FVector2<RealType>& A, const FVector2<RealType>& B, const FVector2<RealType>& C, const FVector2<RealType>& QueryPoint)
	{
		RealType Sign1 = FVector2<RealType>::Orient(A, B, QueryPoint);
		RealType Sign2 = FVector2<RealType>::Orient(B, C, QueryPoint);
		RealType Sign3 = FVector2<RealType>::Orient(C, A, QueryPoint);
		return (Sign1*Sign2 > 0) && (Sign2*Sign3 > 0) && (Sign3*Sign1 > 0);
	}

	/** @return true if QueryPoint is inside triangle */
	bool IsInside(const FVector2<RealType>& QueryPoint) const
	{
		return IsInside(V[0], V[1], V[2], QueryPoint);
	}


};

typedef TTriangle2<float> FTriangle2f;
typedef TTriangle2<double> FTriangle2d;
typedef TTriangle2<int> FTriangle2i;






template<typename RealType>
struct TTriangle3
{
	FVector3<RealType> V[3];

	TTriangle3() {}

	TTriangle3(const FVector3<RealType>& V0, const FVector3<RealType>& V1, const FVector3<RealType>& V2)
	{
		V[0] = V0;
		V[1] = V1;
		V[2] = V2;
	}

	TTriangle3(const FVector3<RealType> VIn[3])
	{
		V[0] = VIn[0];
		V[1] = VIn[1];
		V[2] = VIn[2];
	}

	FVector3<RealType> BarycentricPoint(RealType Bary0, RealType Bary1, RealType Bary2) const
	{
		return Bary0*V[0] + Bary1*V[1] + Bary2*V[2];
	}

	FVector3<RealType> BarycentricPoint(const FVector3<RealType> & BaryCoords) const
	{
		return BaryCoords[0]*V[0] + BaryCoords[1]*V[1] + BaryCoords[2]*V[2];
	}


	/** @return vector that is perpendicular to the plane of this triangle */
	FVector3<RealType> Normal() const
	{
		return VectorUtil::Normal(V[0], V[1], V[2]);
	}

	/** @return centroid of this triangle */
	FVector3<RealType> Centroid() const
	{
		constexpr RealType f = 1.0 / 3.0;
		return FVector3<RealType>(
			(V[0].X + V[1].X + V[2].X) * f,
			(V[0].Y + V[1].Y + V[2].Y) * f,
			(V[0].Z + V[1].Z + V[2].Z) * f
		);
	}

	/** grow the triangle around the centroid */
	void Expand(RealType Delta)
	{
		FVector3<RealType> Centroid(Centroid());
		V[0] += Delta * ((V[0] - Centroid).Normalized());
		V[1] += Delta * ((V[1] - Centroid).Normalized());
		V[2] += Delta * ((V[2] - Centroid).Normalized());
	}
};

typedef TTriangle3<float> FTriangle3f;
typedef TTriangle3<double> FTriangle3d;
typedef TTriangle3<int> FTriangle3i;

