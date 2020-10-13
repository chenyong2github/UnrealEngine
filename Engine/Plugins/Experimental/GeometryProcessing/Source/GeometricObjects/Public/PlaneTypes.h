// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3sharp Plane3

#pragma once

#include "VectorTypes.h"
#include "VectorUtil.h"

template<typename RealType>
struct TPlane3
{
	FVector3<RealType> Normal;
	RealType Constant;

	TPlane3() {}

	TPlane3(FVector3<RealType> Normal, double Constant) : Normal(Normal), Constant(Constant)
	{
	}

	/**
	 * N is specified, c = Dot(N,P) where P is a point on the plane.
	 */
	TPlane3(const FVector3<RealType>& Normal, const FVector3<RealType>& Point) : Normal(Normal), Constant(Normal.Dot(Point))
	{
	}

	/**
	 * N = Cross(P1-P0,P2-P0)/Length(Cross(P1-P0,P2-P0)), c = Dot(N,P0) where
	 * P0, P1, P2 are points on the plane.
	 */
	TPlane3(const FVector3<RealType>& P0, const FVector3<RealType>& P1, const FVector3<RealType>& P2)
	{
		Normal = VectorUtil::Normal(P0, P1, P2);
		Constant = Normal.Dot(P0);
	}


	/**
	 * Compute d = Dot(N,P)-c where N is the plane normal and c is the plane
	 * constant.  This is a signed distance.  The sign of the return value is
	 * positive if the point is on the positive side of the plane, negative if
	 * the point is on the negative side, and zero if the point is on the
	 * plane.
	 */
	double DistanceTo(const FVector3<RealType>& P) const
	{
		return Normal.Dot(P) - Constant;
	}

	/**
	 * The "positive side" of the plane is the half space to which the plane
	 * normal points.  The "negative side" is the other half space.  The
	 * function returns +1 when P is on the positive side, -1 when P is on the
	 * the negative side, or 0 when P is on the plane.
	 */
	int WhichSide(const FVector3<RealType>& P) const
	{
		double Distance = DistanceTo(P);
		if (Distance < 0)
		{
			return -1;
		}
		else if (Distance > 0)
		{
			return +1;
		}
		else
		{
			return 0;
		}
	}




	/**
	 * Compute intersection of Line with Plane
	 * @param LineOrigin origin of line
	 * @param LineDirection direction of line
	 * @param HitPointOut intersection point, or FVector3::MaxVector() if line is parallel to plane
	 * @return true if Line intersects Plane and IntersectionPointOut is valid
	 */
	bool FindLineIntersection(const FVector3<RealType>& LineOrigin, const FVector3<RealType>& LineDirection, FVector3<RealType>& IntersectionPointOut) const
	{
		RealType NormalDot = LineDirection.Dot(Normal);
		if ( TMathUtil<RealType>::Abs(NormalDot) < TMathUtil<RealType>::ZeroTolerance )
		{
			IntersectionPointOut = FVector3<RealType>::MaxVector();
			return false;
		}
		RealType t = -(LineOrigin.Dot(Normal) - Constant) / NormalDot;
		IntersectionPointOut = LineOrigin + t * LineDirection;
		return true;
	}



	/**
	 * Clip line segment defined by two points against plane. Region of Segment on positive side of Plane is kept.
	 * Note that the line may be fully clipped, in that case 0 is returned
	 * @param Point0 first point of segment
	 * @param Point1 second point of segment
	 * @return 0 if line is fully clipped, 1 if line is partially clipped, 2 if line is not clipped
	 */
	int ClipSegment(FVector3<RealType>& Point0, FVector3<RealType>& Point1)
	{
		RealType Dist0 = DistanceTo(Point0);
		RealType Dist1 = DistanceTo(Point1);
		if (Dist0 <= 0 && Dist1 <= 0)
		{
			return 0;
		}
		else if (Dist0 * Dist1 >= 0)
		{
			return 2;
		}

		FVector3<RealType> DirectionVec = Point1 - Point0;
		FVector3<RealType> Direction = DirectionVec.Normalized();
		RealType Length = DirectionVec.Dot(Direction);

		// test if segment is parallel to plane, if so, no intersection
		RealType NormalDot = Direction.Dot(Normal);
		if ( TMathUtil<RealType>::Abs(NormalDot) < TMathUtil<RealType>::ZeroTolerance )
		{
			return 2;
		}

		RealType LineT = -Dist0 / NormalDot;  // calculate line parameter for line/plane intersection
		if (LineT > 0 && LineT < Length)   // verify segment intersection  (should always be true...)
		{
			if (NormalDot < 0)
			{
				Point1 = Point0 + LineT * Direction;
			}
			else
			{
				Point0 += LineT * Direction;
			}
			return 1;
		}
		return 2;
	}

};

typedef TPlane3<float> FPlane3f;
typedef TPlane3<double> FPlane3d;



