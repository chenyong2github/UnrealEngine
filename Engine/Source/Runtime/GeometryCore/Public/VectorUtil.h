// Copyright Epic Games, Inc. All Rights Reserved.

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
	MultiSegment,
	Unknown
};

namespace VectorUtil
{
	using namespace UE::Geometry;

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
	inline bool IsFinite(const UE::Math::TVector<RealType>& V)
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
	inline FVector3<RealType> Normal(const UE::Math::TVector<RealType>& V0, const UE::Math::TVector<RealType>& V1, const UE::Math::TVector<RealType>& V2)
	{
		FVector3<RealType> edge1(V1 - V0);
		FVector3<RealType> edge2(V2 - V0);
		Normalize(edge1);
		Normalize(edge2);
		// Unreal has Left-Hand Coordinate System so we need to reverse this cross-product to get proper triangle normal
		FVector3<RealType> vCross(edge2.Cross(edge1));
		//FVector3<RealType> vCross(edge1.Cross(edge2));
		return Normalized(vCross);
	}

	/**
	 * @return un-normalized direction that is parallel to normal of triangle V0,V1,V2
	 */
	template <typename RealType>
	inline FVector3<RealType> NormalDirection(const UE::Math::TVector<RealType>& V0, const UE::Math::TVector<RealType>& V1, const UE::Math::TVector<RealType>& V2)
	{
		// Unreal has Left-Hand Coordinate System so we need to reverse this cross-product to get proper triangle normal
		return (V2 - V0).Cross(V1 - V0);
		//return (V1 - V0).Cross(V2 - V0);
	}

	/**
	 * @return area of 3D triangle V0,V1,V2
	 */
	template <typename RealType>
	inline RealType Area(const UE::Math::TVector<RealType>& V0, const UE::Math::TVector<RealType>& V1, const UE::Math::TVector<RealType>& V2)
	{
		FVector3<RealType> Edge1(V1 - V0);
		FVector3<RealType> Edge2(V2 - V0);
		FVector3<RealType> Cross = Edge2.Cross(Edge1);
		return (RealType)0.5 * Cross.Length();
	}

	/**
	 * @return area of 2D triangle V0,V1,V2
	 */
	template <typename RealType>
	inline RealType Area(const FVector2<RealType>& V0, const FVector2<RealType>& V1, const FVector2<RealType>& V2)
	{
		FVector2<RealType> Edge1(V1 - V0);
		FVector2<RealType> Edge2(V2 - V0);
		RealType CrossZ = DotPerp(Edge1, Edge2);
		return (RealType)0.5 * TMathUtil<RealType>::Abs(CrossZ);
	}

	/**
	 * @return true if triangle V1,V2,V3 is obtuse
	 */
	template <typename RealType>
	inline bool IsObtuse(const UE::Math::TVector<RealType>& V1, const UE::Math::TVector<RealType>& V2, const UE::Math::TVector<RealType>& V3)
	{
		RealType a2 = DistanceSquared(V1, V2);
		RealType b2 = DistanceSquared(V1, V3);
		RealType c2 = DistanceSquared(V2, V3);
		return (a2 + b2 < c2) || (b2 + c2 < a2) || (c2 + a2 < b2);
	}

	/**
	 * Calculate Normal and Area of triangle V0,V1,V2
	 * @return triangle normal
	 */
	template <typename RealType>
	inline FVector3<RealType> NormalArea(const UE::Math::TVector<RealType>& V0, const UE::Math::TVector<RealType>& V1, const UE::Math::TVector<RealType>& V2, RealType& AreaOut)
	{
		FVector3<RealType> edge1(V1 - V0);
		FVector3<RealType> edge2(V2 - V0);
		// Unreal has Left-Hand Coordinate System so we need to reverse this cross-product to get proper triangle normal
		FVector3d vCross = edge2.Cross(edge1);
		//FVector3d vCross = edge1.Cross(edge2);
		AreaOut = RealType(0.5) * Normalize(vCross);
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
	inline bool EpsilonEqual(const UE::Math::TVector<RealType>& V0, const UE::Math::TVector<RealType>& V1, RealType Epsilon)
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
	inline void MakePerpVectors(const UE::Math::TVector<RealType>& Normal, UE::Math::TVector<RealType>& OutPerp1, UE::Math::TVector<RealType>& OutPerp2)
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
	inline RealType PlaneAngleSignedD(const UE::Math::TVector<RealType>& VFrom, const UE::Math::TVector<RealType>& VTo, const UE::Math::TVector<RealType>& PlaneN)
	{
		FVector3<RealType> vFrom = VFrom - VFrom.Dot(PlaneN) * PlaneN;
		FVector3<RealType> vTo = VTo - VTo.Dot(PlaneN) * PlaneN;
		Normalize(vFrom);
		Normalize(vTo);
		FVector3<RealType> C = vFrom.Cross(vTo);
		if (C.SquaredLength() < TMathUtil<RealType>::ZeroTolerance)
		{ // vectors are parallel
			return vFrom.Dot(vTo) < 0 ? (RealType)180 : (RealType)0;
		}
		RealType fSign = C.Dot(PlaneN) < 0 ? (RealType)-1 : (RealType)1;
		return (RealType)(fSign * AngleD(vFrom, vTo));
	}

	/**
	 * tan(theta/2) = +/- sqrt( (1-cos(theta)) / (1+cos(theta)) )
	 * @return positive value of tan(theta/2) where theta is angle between normalized vectors A and B
	 */
	template <typename RealType>
	RealType VectorTanHalfAngle(const UE::Math::TVector<RealType>& A, const UE::Math::TVector<RealType>& B)
	{
		RealType cosAngle = A.Dot(B);
		RealType sqr = ((RealType)1 - cosAngle) / ((RealType)1 + cosAngle);
		sqr = Clamp(sqr, (RealType)0, TMathUtil<RealType>::MaxReal);
		return TMathUtil<RealType>::Sqrt(sqr);
	}

	/**
	 * Fast cotangent of angle between two vectors (*do not have to be normalized unit vectors*).
	 * cot = cos/sin, both of which can be computed from vector identities
	 * @return cotangent of angle between V1 and V2, or zero if result would be unstable (eg infinity)
	 */ 
	template <typename RealType>
	RealType VectorCot(const UE::Math::TVector<RealType>& V1, const UE::Math::TVector<RealType>& V2)
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
	FVector3<RealType> BarycentricCoords(const UE::Math::TVector<RealType>& Point, const UE::Math::TVector<RealType>& V0, const UE::Math::TVector<RealType>& V1, const UE::Math::TVector<RealType>& V2)
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
	FVector3<RealType> BarycentricCoords(const FVector2<RealType>& Point, const FVector2<RealType>& V0, const FVector2<RealType>& V1, const FVector2<RealType>& V2)
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
	inline RealType TriSolidAngle(UE::Math::TVector<RealType> A, UE::Math::TVector<RealType> B, UE::Math::TVector<RealType> C, const UE::Math::TVector<RealType>& P)
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
	 * Calculate gradient of scalar field values fi,fj,fk defined at corners of triangle Vi,Vj,Vk and interpolated across triangle using linear basis functions.
	 * This gradient is a 3D vector lying in the plane of the triangle (or zero if field is constant).
	 * @return gradient (3D vector) lying in plane of triangle.
	 */
	template <typename RealType>
	inline FVector3<RealType> TriGradient(UE::Math::TVector<RealType> Vi, UE::Math::TVector<RealType> Vj, UE::Math::TVector<RealType> Vk, RealType fi, RealType fj, RealType fk)
	{
		// recenter (better for precision)
		FVector3<RealType> Centroid = (Vi + Vj + Vk) / (RealType)3;
		Vi -= Centroid; Vj -= Centroid; Vk -= Centroid;
		// calculate tangent-normal frame
		FVector3<RealType> Normal = VectorUtil::Normal<RealType>(Vi, Vj, Vk);
		FVector3<RealType> Perp0, Perp1;
		VectorUtil::MakePerpVectors<RealType>(Normal, Perp0, Perp1);
		// project points to triangle plane coordinates
		FVector2<RealType> vi(Vi.Dot(Perp0), Vi.Dot(Perp1));
		FVector2<RealType> vj(Vj.Dot(Perp0), Vj.Dot(Perp1));
		FVector2<RealType> vk(Vk.Dot(Perp0), Vk.Dot(Perp1));
		// calculate gradient
		FVector2<RealType> GradX = (fj-fi)*PerpCW(vi-vk) + (fk-fi)*PerpCW(vj-vi);
		// map back to 3D vector in triangle plane
		RealType AreaScale = (RealType)1 / ((RealType)2 * VectorUtil::Area<RealType>(Vi, Vj, Vk));
		return AreaScale * (GradX.X * Perp0 + GradX.Y * Perp1);
	}



	/**
	 * @return angle between vectors (A-CornerPt) and (B-CornerPt)
	 */
	template<typename RealType>
	inline RealType OpeningAngleD(UE::Math::TVector<RealType> A, UE::Math::TVector<RealType> B, const UE::Math::TVector<RealType>& P)
	{
		A -= P; 
		Normalize(A);
		B -= P;
		Normalize(B);
		return AngleD(A, B);
	}



	/**
	 * @return sign of Bitangent relative to Normal and Tangent
	 */
	template<typename RealType>
	inline RealType BitangentSign(const UE::Math::TVector<RealType>& NormalIn, const UE::Math::TVector<RealType>& TangentIn, const UE::Math::TVector<RealType>& BitangentIn)
	{
		// following math from RenderUtils.h::GetBasisDeterminantSign()
		RealType Cross00 = BitangentIn.Y*NormalIn.Z - BitangentIn.Z*NormalIn.Y;
		RealType Cross10 = BitangentIn.Z*NormalIn.X - BitangentIn.X*NormalIn.Z;
		RealType Cross20 = BitangentIn.X*NormalIn.Y - BitangentIn.Y*NormalIn.X;
		RealType Determinant = TangentIn.X*Cross00 + TangentIn.Y*Cross10 + TangentIn.Z*Cross20;
		return (Determinant < 0) ? (RealType)-1 : (RealType)1;
	}

	/**
	 * @return Bitangent vector based on given Normal, Tangent, and Sign value (+1/-1)
	 */
	template<typename RealType>
	inline FVector3<RealType> Bitangent(const UE::Math::TVector<RealType>& NormalIn, const UE::Math::TVector<RealType>& TangentIn, RealType BitangentSign)
	{
		return BitangentSign * FVector3<RealType>(
			NormalIn.Y*TangentIn.Z - NormalIn.Z*TangentIn.Y,
			NormalIn.Z*TangentIn.X - NormalIn.X*TangentIn.Z,
			NormalIn.X*TangentIn.Y - NormalIn.Y*TangentIn.X);
	}

	/**
	 * @return Tangent-Space vector based on given Normal and Bitangent
	 */
	template<typename RealType>
	inline FVector3<RealType> TangentFromBitangent(const UE::Math::TVector<RealType>& NormalIn, const UE::Math::TVector<RealType>& BitangentIn)
	{
		return BitangentIn.Cross(NormalIn);
	}

	/**
	 * @return Bitangent vector based on given Normal and Tangent
	 */
	template<typename RealType>
	inline FVector3<RealType> BitangentFromTangent(const UE::Math::TVector<RealType>& NormalIn, const UE::Math::TVector<RealType>& TangentIn)
	{
		return NormalIn.Cross(TangentIn);
	}

	/// @return Aspect ratio of triangle 
	inline double AspectRatio(const FVector3d& v1, const FVector3d& v2, const FVector3d& v3)
	{
		double a = Distance(v1, v2), b = Distance(v2, v3), c = Distance(v3, v1);
		double s = (a + b + c) / 2.0;
		return (a * b * c) / (8.0 * (s - a) * (s - b) * (s - c));
	}


}; // namespace VectorUtil
