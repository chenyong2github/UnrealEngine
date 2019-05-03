// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// port of geometry3Sharp Ray3

#pragma once

#include "Math/UnrealMath.h"
#include "VectorTypes.h"
#include "Math/Ray.h"

/*
 * 3D Ray stored as Origin point and normalized Direction vector
 */
template<typename RealType>
class FRay3
{
public:
	/** Origin point */
	FVector3<RealType> Origin;
	/** Direction vector, always normalized */
	FVector3<RealType> Direction;

	/** Construct ray at origin pointed down Z axis */
	FRay3()
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
	FRay3(const FVector3<RealType>& Origin, const FVector3<RealType>& Direction, bool bIsNormalized = false)
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


	// conversion operators

	inline operator FRay() const
	{
		return FRay((FVector)Origin, (FVector)Direction);
	}
	inline FRay3(const FRay & RayIn)
	{
		Origin = (FVector3<RealType>)RayIn.Origin;
		Direction = (FVector3<RealType>)RayIn.Direction;
	}


};
typedef FRay3<float> FRay3f;
typedef FRay3<double> FRay3d;

