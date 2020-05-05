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
};

typedef TPlane3<float> FPlane3f;
typedef TPlane3<double> FPlane3d;



