// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"

// these should go somewhere else?

enum class EIntersectionResult
{
	NotComputed,
	Intersects,
	NoIntersection,
	InvalidQuery
};

enum class EIntersectionType
{
	Empty,
	Point,
	Segment,
	Line,
	Polygon,
	Plane,
	Unknown
};

namespace VectorUtil
{
	template <typename RealType>
	inline bool IsFinite(const FVector2<RealType>& V)
	{
		return TMathUtil<RealType>::IsFinite(V.X) && TMathUtil<RealType>::IsFinite(V.Y);
	}
	template <typename RealType>
	inline bool IsFinite(const FVector3<RealType>& V)
	{
		return TMathUtil<RealType>::IsFinite(V.X) && TMathUtil<RealType>::IsFinite(V.Y) && TMathUtil<RealType>::IsFinite(V.Z);
	}


	template <typename RealType>
	inline RealType Clamp(RealType Value, RealType MinValue, RealType MaxValue)
	{
		return (Value < MinValue) ? MinValue : ((Value > MaxValue) ? MaxValue : Value);
	}

	template <typename RealType>
	inline FVector3<RealType> Normal(const FVector3<RealType>& V0, const FVector3<RealType>& V1, const FVector3<RealType>& V2)
	{
		FVector3<RealType> edge1(V1 - V0);
		FVector3<RealType> edge2(V2 - V0);
		edge1.Normalize();
		edge2.Normalize();
		// Unreal has Left-Hand Coordinate System so we need to reverse this cross-product to get proper triangle normal
		FVector3<RealType> vCross(edge2.Cross(edge1));
		//FVector3<RealType> vCross(edge1.Cross(edge2));
		return vCross.Normalized();
	}

	template <typename RealType>
	inline FVector3<RealType> FastNormalDirection(const FVector3<RealType>& V0, const FVector3<RealType>& V1, const FVector3<RealType>& V2)
	{
		// Unreal has Left-Hand Coordinate System so we need to reverse this cross-product to get proper triangle normal
		return (V2 - V0).Cross(V1 - V0);
		//return (V1 - V0).Cross(V2 - V0);
	}

	template <typename RealType>
	inline RealType Area(const FVector3<RealType>& V0, const FVector3<RealType>& V1, const FVector3<RealType>& V2)
	{
		FVector3<RealType> edge1(V1 - V0);
		FVector3<RealType> edge2(V2 - V0);
		RealType Dot = edge1.Dot(edge2);
		return (RealType)(0.5 * sqrt(edge1.SquaredLength() * edge2.SquaredLength() - Dot * Dot));
	}
	template <typename RealType>
	inline RealType Area(const FVector2<RealType>& V0, const FVector2<RealType>& V1, const FVector2<RealType>& V2)
	{
		FVector2<RealType> edge1(V1 - V0);
		FVector2<RealType> edge2(V2 - V0);
		RealType Dot = edge1.Dot(edge2);
		return (RealType)(0.5 * sqrt(edge1.SquaredLength() * edge2.SquaredLength() - Dot * Dot));
	}

	template <typename RealType>
	inline bool IsObtuse(const FVector3<RealType>& V1, const FVector3<RealType>& V2, const FVector3<RealType>& V3)
	{
		RealType a2 = V1.DistanceSquared(V2);
		RealType b2 = V1.DistanceSquared(V3);
		RealType c2 = V2.DistanceSquared(V3);
		return (a2 + b2 < c2) || (b2 + c2 < a2) || (c2 + a2 < b2);
	}

	template <typename RealType>
	inline FVector3<RealType> FastNormalArea(const FVector3<RealType>& V0, const FVector3<RealType>& V1, const FVector3<RealType>& V2, double& Area)
	{
		FVector3<RealType> edge1(V1 - V0);
		FVector3<RealType> edge2(V2 - V0);
		// Unreal has Left-Hand Coordinate System so we need to reverse this cross-product to get proper triangle normal
		FVector3d vCross = edge2.Cross(edge1);
		//FVector3d vCross = edge1.Cross(edge2);
		Area = RealType(0.5) * vCross.Normalize();
		return vCross;
	}

	template <typename RealType>
	inline bool EpsilonEqual(RealType A, RealType B, RealType Epsilon)
	{
		return TMathUtil<RealType>::Abs(A - B) < Epsilon;
	}

	template <typename RealType>
	inline bool EpsilonEqual(const FVector2<RealType>& V0, const FVector2<RealType>& V1, RealType Epsilon)
	{
		return EpsilonEqual(V0.X, V1.X, Epsilon) && EpsilonEqual(V0.Y, V1.Y, Epsilon);
	}
	template <typename RealType>
	inline bool EpsilonEqual(const FVector3<RealType>& V0, const FVector3<RealType>& V1, RealType Epsilon)
	{
		return EpsilonEqual(V0.X, V1.X, Epsilon) && EpsilonEqual(V0.Y, V1.Y, Epsilon) && EpsilonEqual(V0.Z, V1.Z, Epsilon);
	}

	/**
	 * Returns two vectors perpendicular to n, as efficiently as possible.
	 * Duff et all method, from https://graphics.pixar.com/library/OrthonormalB/paper.pdf
	 */
	template <typename RealType>
	inline void MakePerpVectors(const FVector3<RealType>& Normal, FVector3<RealType>& OutPerp1, FVector3<RealType>& OutPerp2)
	{
		if (Normal.Z < (RealType)0)
		{
			RealType A = (RealType)1 / ((RealType)1 - Normal.Z);
			RealType B = Normal.X * Normal.Y * A;
			OutPerp1.X = (RealType)1 - Normal.X * Normal.X * A;
			OutPerp1.Y = -B;
			OutPerp1.Z = Normal.X;
			OutPerp2.X = B;
			OutPerp2.Y = Normal.Y * Normal.Y * A - (RealType)1;
			OutPerp2.Z = -Normal.Y;
		}
		else
		{
			RealType A = (RealType)1 / ((RealType)1 + Normal.Z);
			RealType B = -Normal.X * Normal.Y * A;
			OutPerp1.X = (RealType)1 - Normal.X * Normal.X * A;
			OutPerp1.Y = B;
			OutPerp1.Z = -Normal.X;
			OutPerp2.X = B;
			OutPerp2.Y = (RealType)1 - Normal.Y * Normal.Y * A;
			OutPerp2.Z = -Normal.Y;
		}
	}

	template <typename RealType>
	inline double PlaneAngleSignedD(const FVector3<RealType>& VFromIn, const FVector3<RealType>& VToIn, const FVector3<RealType>& PlaneN)
	{
		FVector3<RealType> vFrom = VFromIn - VFromIn.Dot(PlaneN) * PlaneN;
		FVector3<RealType> vTo = VToIn - VToIn.Dot(PlaneN) * PlaneN;
		vFrom.Normalize();
		vTo.Normalize();
		FVector3<RealType> C = vFrom.Cross(vTo);
		if (C.SquaredLength() < TMathUtil<RealType>::ZeroTolerance)
		{ // vectors are parallel
			return vFrom.Dot(vTo) < 0 ? 180.0 : 0;
		}
		RealType fSign = C.Dot(PlaneN) < 0 ? (RealType)-1 : (RealType)1;
		return (RealType)(fSign * vFrom.AngleD(vTo));
	}

	/**
	 * tan(theta/2) = +/- sqrt( (1-cos(theta)) / (1+cos(theta)) )
	 * This function returns positive Value
	 */
	template <typename RealType>
	RealType VectorTanHalfAngle(const FVector3<RealType>& A, const FVector3<RealType>& B)
	{
		RealType cosAngle = A.Dot(B);
		RealType sqr = ((RealType)1 - cosAngle) / ((RealType)1 + cosAngle);
		sqr = Clamp(sqr, (RealType)0, TMathUtil<RealType>::MaxReal);
		return (RealType)sqrt(sqr);
	}

	//! fast cotangent between two normalized vectors
	//! cot = cos/sin, both of which can be computed from vector identities
	//! returns zero if result would be unstable (eg infinity)
	template <typename RealType>
	RealType VectorCot(const FVector3<RealType>& V1, const FVector3<RealType>& V2)
	{
		// formula from http://www.geometry.caltech.edu/pubs/DMSB_III.pdf
		RealType fDot = V1.Dot(V2);
		RealType lensqr1 = V1.SquaredLength();
		RealType lensqr2 = V2.SquaredLength();
		RealType d = Clamp(lensqr1 * lensqr2 - fDot * fDot, (RealType)0.0, TMathUtil<RealType>::MaxReal);
		if (d < TMathUtil<RealType>::ZeroTolerance)
			return (RealType)0;
		else
			return fDot / (RealType)sqrt(d);
	}

	template <typename RealType>
	inline double TriSolidAngle(FVector3<RealType> A, FVector3<RealType> B, FVector3<RealType> C, const FVector3<RealType>& P)
	{
		A -= P;
		B -= P;
		C -= P;
		RealType la = A.Length(), lb = B.Length(), lc = C.Length();
		RealType top = (la * lb * lc) + A.Dot(B) * lc + B.Dot(C) * la + C.Dot(A) * lb;
		RealType bottom = A.X * (B.Y * C.Z - C.Y * B.Z) - A.Y * (B.X * C.Z - C.X * B.Z) + A.Z * (B.X * C.Y - C.X * B.Y);
		// -2 instead of 2 to account for UE winding
		return RealType(-2.0) * atan2(bottom, top);
	}

}; // namespace VectorUtil
