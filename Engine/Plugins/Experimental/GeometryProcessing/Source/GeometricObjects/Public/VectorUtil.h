// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MathUtil.h"
#include "VectorTypes.h"


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
	/**
	 * @return true if all components of V are finite
	 */
	template <typename RealType>
	inline bool IsFinite(const FVector2<RealType>& V)
	{
		return TMathUtil<RealType>::IsFinite(V.X) && TMathUtil<RealType>::IsFinite(V.Y);
	}

	/**
	 * @return true if all components of V are finite
	 */
	template <typename RealType>
	inline bool IsFinite(const FVector3<RealType>& V)
	{
		return TMathUtil<RealType>::IsFinite(V.X) && TMathUtil<RealType>::IsFinite(V.Y) && TMathUtil<RealType>::IsFinite(V.Z);
	}


	/**
	 * @return input Value clamped to range [MinValue, MaxValue]
	 */
	template <typename RealType>
	inline RealType Clamp(RealType Value, RealType MinValue, RealType MaxValue)
	{
		return (Value < MinValue) ? MinValue : ((Value > MaxValue) ? MaxValue : Value);
	}

	/**
	 * @return normalized vector that is perpendicular to triangle V0,V1,V2  (triangle normal)
	 */
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

	/**
	 * @return un-normalized direction that is parallel to normal of triangle V0,V1,V2
	 */
	template <typename RealType>
	inline FVector3<RealType> NormalDirection(const FVector3<RealType>& V0, const FVector3<RealType>& V1, const FVector3<RealType>& V2)
	{
		// Unreal has Left-Hand Coordinate System so we need to reverse this cross-product to get proper triangle normal
		return (V2 - V0).Cross(V1 - V0);
		//return (V1 - V0).Cross(V2 - V0);
	}

	/**
	 * @return area of 3D triangle V0,V1,V2
	 */
	template <typename RealType>
	inline RealType Area(const FVector3<RealType>& V0, const FVector3<RealType>& V1, const FVector3<RealType>& V2)
	{
		FVector3<RealType> edge1(V1 - V0);
		FVector3<RealType> edge2(V2 - V0);
		RealType Dot = edge1.Dot(edge2);
		return (RealType)0.5 * TMathUtil<RealType>::Sqrt(edge1.SquaredLength() * edge2.SquaredLength() - Dot * Dot);
	}

	/**
	 * @return area of 2D triangle V0,V1,V2
	 */
	template <typename RealType>
	inline RealType Area(const FVector2<RealType>& V0, const FVector2<RealType>& V1, const FVector2<RealType>& V2)
	{
		FVector2<RealType> edge1(V1 - V0);
		FVector2<RealType> edge2(V2 - V0);
		RealType Dot = edge1.Dot(edge2);
		return (RealType)0.5 * TMathUtil<RealType>::Sqrt(edge1.SquaredLength() * edge2.SquaredLength() - Dot * Dot);
	}

	/**
	 * @return true if triangle V1,V2,V3 is obtuse
	 */
	template <typename RealType>
	inline bool IsObtuse(const FVector3<RealType>& V1, const FVector3<RealType>& V2, const FVector3<RealType>& V3)
	{
		RealType a2 = V1.DistanceSquared(V2);
		RealType b2 = V1.DistanceSquared(V3);
		RealType c2 = V2.DistanceSquared(V3);
		return (a2 + b2 < c2) || (b2 + c2 < a2) || (c2 + a2 < b2);
	}

	/**
	 * Calculate Normal and Area of triangle V0,V1,V2
	 * @return triangle normal
	 */
	template <typename RealType>
	inline FVector3<RealType> NormalArea(const FVector3<RealType>& V0, const FVector3<RealType>& V1, const FVector3<RealType>& V2, RealType& AreaOut)
	{
		FVector3<RealType> edge1(V1 - V0);
		FVector3<RealType> edge2(V2 - V0);
		// Unreal has Left-Hand Coordinate System so we need to reverse this cross-product to get proper triangle normal
		FVector3d vCross = edge2.Cross(edge1);
		//FVector3d vCross = edge1.Cross(edge2);
		AreaOut = RealType(0.5) * vCross.Normalize();
		return vCross;
	}

	/** @return true if Abs(A-B) is less than Epsilon */
	template <typename RealType>
	inline bool EpsilonEqual(RealType A, RealType B, RealType Epsilon)
	{
		return TMathUtil<RealType>::Abs(A - B) < Epsilon;
	}

	/** @return true if all coordinates of V0 and V1 are within Epsilon of eachother */
	template <typename RealType>
	inline bool EpsilonEqual(const FVector2<RealType>& V0, const FVector2<RealType>& V1, RealType Epsilon)
	{
		return EpsilonEqual(V0.X, V1.X, Epsilon) && EpsilonEqual(V0.Y, V1.Y, Epsilon);
	}

	/** @return true if all coordinates of V0 and V1 are within Epsilon of eachother */
	template <typename RealType>
	inline bool EpsilonEqual(const FVector3<RealType>& V0, const FVector3<RealType>& V1, RealType Epsilon)
	{
		return EpsilonEqual(V0.X, V1.X, Epsilon) && EpsilonEqual(V0.Y, V1.Y, Epsilon) && EpsilonEqual(V0.Z, V1.Z, Epsilon);
	}


	/** @return 0/1/2 index of smallest value in Vector3 */
	template <typename ValueVecType>
	inline int Min3Index(const ValueVecType& Vector3)
	{
		if (Vector3[0] <= Vector3[1])
		{
			return Vector3[0] <= Vector3[2] ? 0 : 2;
		}
		else
		{
			return (Vector3[1] <= Vector3[2]) ? 1 : 2;
		}
	}


	/**
	 * Calculates two vectors perpendicular to input Normal, as efficiently as possible.
	 */
	template <typename RealType>
	inline void MakePerpVectors(const FVector3<RealType>& Normal, FVector3<RealType>& OutPerp1, FVector3<RealType>& OutPerp2)
	{
		// Duff et al method, from https://graphics.pixar.com/library/OrthonormalB/paper.pdf
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

	/**
	 * Calculates one vector perpendicular to input Normal, as efficiently as possible.
	 */
	template <typename RealType>
	inline void MakePerpVector(const FVector3<RealType>& Normal, FVector3<RealType>& OutPerp1)
	{
		// Duff et al method, from https://graphics.pixar.com/library/OrthonormalB/paper.pdf
		if (Normal.Z < (RealType)0)
		{
			RealType A = (RealType)1 / ((RealType)1 - Normal.Z);
			RealType B = Normal.X * Normal.Y * A;
			OutPerp1.X = (RealType)1 - Normal.X * Normal.X * A;
			OutPerp1.Y = -B;
			OutPerp1.Z = Normal.X;
		}
		else
		{
			RealType A = (RealType)1 / ((RealType)1 + Normal.Z);
			RealType B = -Normal.X * Normal.Y * A;
			OutPerp1.X = (RealType)1 - Normal.X * Normal.X * A;
			OutPerp1.Y = B;
			OutPerp1.Z = -Normal.X;
		}
	}


	/**
	 * Calculates angle between VFrom and VTo after projection onto plane with normal defined by PlaneN
	 * @return angle in degrees
	 */
	template <typename RealType>
	inline RealType PlaneAngleSignedD(const FVector3<RealType>& VFrom, const FVector3<RealType>& VTo, const FVector3<RealType>& PlaneN)
	{
		FVector3<RealType> vFrom = VFrom - VFrom.Dot(PlaneN) * PlaneN;
		FVector3<RealType> vTo = VTo - VTo.Dot(PlaneN) * PlaneN;
		vFrom.Normalize();
		vTo.Normalize();
		FVector3<RealType> C = vFrom.Cross(vTo);
		if (C.SquaredLength() < TMathUtil<RealType>::ZeroTolerance)
		{ // vectors are parallel
			return vFrom.Dot(vTo) < 0 ? (RealType)180 : (RealType)0;
		}
		RealType fSign = C.Dot(PlaneN) < 0 ? (RealType)-1 : (RealType)1;
		return (RealType)(fSign * vFrom.AngleD(vTo));
	}

	/**
	 * tan(theta/2) = +/- sqrt( (1-cos(theta)) / (1+cos(theta)) )
	 * @return positive value of tan(theta/2) where theta is angle between normalized vectors A and B
	 */
	template <typename RealType>
	RealType VectorTanHalfAngle(const FVector3<RealType>& A, const FVector3<RealType>& B)
	{
		RealType cosAngle = A.Dot(B);
		RealType sqr = ((RealType)1 - cosAngle) / ((RealType)1 + cosAngle);
		sqr = Clamp(sqr, (RealType)0, TMathUtil<RealType>::MaxReal);
		return TMathUtil<RealType>::Sqrt(sqr);
	}

	/**
	 * Fast cotangent of angle between two vectors.
	 * cot = cos/sin, both of which can be computed from vector identities
	 * @return cotangent of angle between V1 and V2, or zero if result would be unstable (eg infinity)
	 */ 
	template <typename RealType>
	RealType VectorCot(const FVector3<RealType>& V1, const FVector3<RealType>& V2)
	{
		// formula from http://www.geometry.caltech.edu/pubs/DMSB_III.pdf
		RealType fDot = V1.Dot(V2);
		RealType lensqr1 = V1.SquaredLength();
		RealType lensqr2 = V2.SquaredLength();
		RealType d = Clamp(lensqr1 * lensqr2 - fDot * fDot, (RealType)0.0, TMathUtil<RealType>::MaxReal);
		if (d < TMathUtil<RealType>::ZeroTolerance)
		{
			return (RealType)0;
		}
		else
		{
			return fDot / TMathUtil<RealType>::Sqrt(d);
		}
	}

	/**
	 * Compute barycentric coordinates/weights of vPoint inside 3D triangle (V0,V1,V2). 
	 * If point is in triangle plane and inside triangle, coords will be positive and sum to 1.
	 * ie if result is a, then vPoint = a.x*V0 + a.y*V1 + a.z*V2.
	 * TODO: make robust to degenerate triangles?
	 */
	template <typename RealType>
	FVector3<RealType> BarycentricCoords(const FVector3<RealType>& Point, const FVector3<RealType>& V0, const FVector3<RealType>& V1, const FVector3<RealType>& V2)
	{
		FVector3<RealType> kV02 = V0 - V2;
		FVector3<RealType> kV12 = V1 - V2;
		FVector3<RealType> kPV2 = Point - V2;
		RealType fM00 = kV02.Dot(kV02);
		RealType fM01 = kV02.Dot(kV12);
		RealType fM11 = kV12.Dot(kV12);
		RealType fR0 = kV02.Dot(kPV2);
		RealType fR1 = kV12.Dot(kPV2);
		RealType fDet = fM00 * fM11 - fM01 * fM01;
		RealType fInvDet = 1.0 / fDet;
		RealType fBary1 = (fM11 * fR0 - fM01 * fR1) * fInvDet;
		RealType fBary2 = (fM00 * fR1 - fM01 * fR0) * fInvDet;
		RealType fBary3 = 1.0 - fBary1 - fBary2;
		return FVector3<RealType>(fBary1, fBary2, fBary3);
	}

	/**
	* Compute barycentric coordinates/weights of vPoint inside 2D triangle (V0,V1,V2). 
	* If point is inside triangle, coords will be positive and sum to 1.
	* ie if result is a, then vPoint = a.x*V0 + a.y*V1 + a.z*V2.
	* TODO: make robust to degenerate triangles?
	*/
	template <typename RealType>
	FVector3<RealType> BarycentricCoords(const FVector3<RealType>& Point, const FVector2<RealType>& V0, const FVector2<RealType>& V1, const FVector2<RealType>& V2)
	{
		FVector2<RealType> kV02 = V0 - V2;
		FVector2<RealType> kV12 = V1 - V2;
		FVector2<RealType> kPV2 = Point - V2;
		RealType fM00 = kV02.Dot(kV02);
		RealType fM01 = kV02.Dot(kV12);
		RealType fM11 = kV12.Dot(kV12);
		RealType fR0 = kV02.Dot(kPV2);
		RealType fR1 = kV12.Dot(kPV2);
		RealType fDet = fM00 * fM11 - fM01 * fM01;
		RealType fInvDet = 1.0 / fDet;
		RealType fBary1 = (fM11 * fR0 - fM01 * fR1) * fInvDet;
		RealType fBary2 = (fM00 * fR1 - fM01 * fR0) * fInvDet;
		RealType fBary3 = 1.0 - fBary1 - fBary2;
		return FVector3<RealType>(fBary1, fBary2, fBary3);
	}

	/**
	 * @return solid angle at point P for triangle A,B,C
	 */
	template <typename RealType>
	inline RealType TriSolidAngle(FVector3<RealType> A, FVector3<RealType> B, FVector3<RealType> C, const FVector3<RealType>& P)
	{
		// Formula from https://igl.ethz.ch/projects/winding-number/
		A -= P;
		B -= P;
		C -= P;
		RealType la = A.Length(), lb = B.Length(), lc = C.Length();
		RealType top = (la * lb * lc) + A.Dot(B) * lc + B.Dot(C) * la + C.Dot(A) * lb;
		RealType bottom = A.X * (B.Y * C.Z - C.Y * B.Z) - A.Y * (B.X * C.Z - C.X * B.Z) + A.Z * (B.X * C.Y - C.X * B.Y);
		// -2 instead of 2 to account for UE winding
		return RealType(-2.0) * atan2(bottom, top);
	}

	/**
	 * @return angle between vectors (A-CornerPt) and (B-CornerPt)
	 */
	template<typename RealType>
	inline RealType OpeningAngleD(FVector3<RealType> A, FVector3<RealType> B, const FVector3<RealType>& P)
	{
		A -= P; 
		A.Normalize();
		B -= P;
		B.Normalize();
		return A.AngleD(B);
	}



	/**
	 * @return sign of Binormal/Bitangent relative to Normal and Tangent
	 */
	template<typename RealType>
	inline RealType BinormalSign(const FVector3<RealType>& NormalIn, const FVector3<RealType>& TangentIn, const FVector3<RealType>& BinormalIn)
	{
		// following math from RenderUtils.h::GetBasisDeterminantSign()
		RealType Cross00 = BinormalIn.Y*NormalIn.Z - BinormalIn.Z*NormalIn.Y;
		RealType Cross10 = BinormalIn.Z*NormalIn.X - BinormalIn.X*NormalIn.Z;
		RealType Cross20 = BinormalIn.X*NormalIn.Y - BinormalIn.Y*NormalIn.X;
		RealType Determinant = TangentIn.X*Cross00 + TangentIn.Y*Cross10 + TangentIn.Z*Cross20;
		return (Determinant < 0) ? (RealType)-1 : (RealType)1;
	}

	/**
	 * @return Binormal vector based on given Normal, Tangent, and Sign value (+1/-1)
	 */
	template<typename RealType>
	inline FVector3<RealType> Binormal(const FVector3<RealType>& NormalIn, const FVector3<RealType>& TangentIn, RealType BinormalSign)
	{
		return BinormalSign * FVector3<RealType>(
			NormalIn.Y*TangentIn.Z - NormalIn.Z*TangentIn.Y,
			NormalIn.Z*TangentIn.X - NormalIn.X*TangentIn.Z,
			NormalIn.X*TangentIn.Y - NormalIn.Y*TangentIn.X);
	}




}; // namespace VectorUtil
