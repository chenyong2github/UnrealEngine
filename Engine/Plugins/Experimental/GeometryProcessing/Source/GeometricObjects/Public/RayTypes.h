// Copyright Epic Games, Inc. All Rights Reserved.

// port of geometry3Sharp Ray3

#pragma once

#include "Math/UnrealMath.h"
#include "VectorTypes.h"
#include "Math/Ray.h"

/*
 * 3D Ray stored as Origin point and normalized Direction vector
 */
template<typename RealType>
class TRay3
{
public:
	/** Origin point */
	FVector3<RealType> Origin;
	/** Direction vector, always normalized */
	FVector3<RealType> Direction;

	/** Construct ray at origin pointed down Z axis */
	TRay3()
	{
		Origin = FVector3<RealType>::Zero();
		Direction = FVector3<RealType>::UnitZ();
	}

	/**
	 * Construct ray from origin and direction
	 * @param Origin origin point
	 * @param Direction direction vector. Must be normalized if bIsNormalized=true
	 * @param bIsNormalized if true, Direction will not be re-normalized
	 */
	TRay3(const FVector3<RealType>& Origin, const FVector3<RealType>& Direction, bool bIsNormalized = false)
	{
		this->Origin = Origin;
		this->Direction = Direction;
		if (bIsNormalized == false)
		{
			this->Direction.Normalize();
		}
	}

	/**
	 * @return point on ray at given (signed) Distance from the ray Origin
	 */
	FVector3<RealType> PointAt(RealType Distance) const
	{
		return Origin + Distance * Direction;
	}


	/**
	 * @return ray parameter (ie positive distance from Origin) at nearest point on ray to QueryPoint
	 */
	inline RealType Project(const FVector3<RealType>& QueryPoint) const
	{
		RealType LineParam = (QueryPoint - Origin).Dot(Direction);
		return (LineParam < 0) ? 0 : LineParam;
	}

	/**
	 * @return smallest squared distance from ray to QueryPoint
	 */
	inline RealType DistanceSquared(const FVector3<RealType>& QueryPoint) const
	{
		RealType LineParam = (QueryPoint - Origin).Dot(Direction);
		if (LineParam < 0)
		{
			return Origin.DistanceSquared(QueryPoint);
		}
		else
		{
			FVector3<RealType> NearestPt = Origin + LineParam * Direction;
			return NearestPt.DistanceSquared(QueryPoint);
		}
	}

	/**
	 * @return smallest squared distance from ray to QueryPoint
	 */
	inline RealType Distance(const FVector3<RealType>& QueryPoint) const
	{
		return TMathUtil<RealType>::Sqrt(DistanceSquared(QueryPoint));
	}

	/**
	 * @return nearest point on line to QueryPoint
	 */
	inline FVector3<RealType> NearestPoint(const FVector3<RealType>& QueryPoint) const
	{
		RealType LineParam = (QueryPoint - Origin).Dot(Direction);
		if (LineParam < 0)
		{
			return Origin;
		}
		else
		{
			return Origin + LineParam * Direction;
		}
	}



	// conversion operators

	explicit inline operator FRay() const
	{
		return FRay((FVector)Origin, (FVector)Direction);
	}
	inline TRay3(const FRay & RayIn)
	{
		Origin = FVector3<RealType>(RayIn.Origin);
		Direction = FVector3<RealType>(RayIn.Direction);
	}


};
typedef TRay3<float> FRay3f;
typedef TRay3<double> FRay3d;

