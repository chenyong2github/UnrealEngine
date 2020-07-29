// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/UnrealMath.h"
#include "VectorTypes.h"

/*
 * 3D Sphere stored as Center point and Radius
 */
template<typename T>
struct TSphere3
{
public:
	/** Center of the sphere */
	FVector3<T> Center = FVector3<T>::Zero();
	/** Radius of the sphere */
	T Radius = (T)0;

	TSphere3() = default;

	TSphere3(const FVector3<T>& CenterIn, T RadiusIn)
		: Center(CenterIn), Radius(RadiusIn) {}

	/** @return Diameter of sphere */
	T Diameter() const
	{
		return (T)2 * Radius;
	}

	/** @return Circumference of sphere */
	T Circumference() const
	{
		return (T)2 * TMathUtil<T>::Pi * Radius;
	}

	/** @return Area of sphere */
	T Area() const
	{
		return Area(Radius);
	}

	/** @return Volume of sphere */
	T Volume() const
	{
		return Volume(Radius);
	}

	/** @return true if Sphere contains given Point */
	bool Contains(const FVector3<T>& Point) const
	{
		T DistSqr = Center.DistanceSquared(Point);
		return DistSqr <= Radius * Radius;
	}

	/** @return true if Sphere contains given OtherSphere */
	bool Contains(const TSphere3<T>& OtherSphere) const
	{
		T CenterDist = Center.Distance(OtherSphere.Center);
		return (CenterDist + OtherSphere.Radius) <= Radius;
	}


	/**
	 * @return minimum squared distance from Point to Sphere surface for points outside sphere, 0 for points inside
	 */
	inline T DistanceSquared(const FVector3<T>& Point) const
	{
		const T PosDistance = TMathUtil<T>::Max(SignedDistance(Point), (T)0);
		return PosDistance * PosDistance;
	}

	/**
	 * @return signed distance from Point to Sphere surface. Points inside sphere return negative distance.
	 */
	inline T SignedDistance(const FVector3<T>& Point) const
	{
		return Center.Distance(Point) - Radius;
	}


	//
	// Sphere utility functions
	//

	/** @return Area of sphere with given Radius */
	static T Area(T Radius)
	{
		return (T)(4) * TMathUtil<T>::Pi * Radius * Radius;
	}

	/** @return Volume of sphere with given Radius */
	static T Volume(T Radius)
	{
		return (T)(4.0 / 3.0) * TMathUtil<T>::Pi * Radius * Radius * Radius;
	}
};


typedef TSphere3<float> FSphere3f;
typedef TSphere3<double> FSphere3d;