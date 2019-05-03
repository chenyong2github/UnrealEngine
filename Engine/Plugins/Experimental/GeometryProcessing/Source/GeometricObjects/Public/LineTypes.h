// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// line types ported from WildMagic and geometry3Sharp

#pragma once

#include "Math/UnrealMath.h"
#include "VectorTypes.h"

/**
 * FLine2 is a two-dimensional infinite line.
 * The line is stored in (Center,Direction) form.
 */
template<typename T>
struct FLine2
{
	/** Origin / Center Point of Line */
	FVector2<T> Origin;

	/** Direction of Line, Normalized */
	FVector2<T> Direction;

	/**
	 * Construct default line along X axis
	 */
	FLine2()
	{
		Origin = FVector2<T>::Zero();
		Direction = FVector2<T>::UnitX();
	}

	/**
	 * Construct line with given Origin and Direction
	 */
	FLine2(const FVector2<T>& OriginIn, const FVector2<T>& DirectionIn)
		: Origin(OriginIn), Direction(DirectionIn)
	{
	}


	/**
	 * @return line between two points
	 */
	static FLine2<T> FromPoints(const FVector2<T>& Point0, const FVector2<T>& Point1)
	{
		return FLine2<T>(Point0, (Point1 - Point0).Normalized());
	}


	/**
	 * @return point on line at given line parameter value (distance along line from origin)
	 */
	inline FVector2<T> PointAt(double LineParameter) const
	{
		return Origin + LineParameter * Direction;
	}


	/**
	 * @return line parameter (ie distance from Origin) at nearest point on line to QueryPoint
	 */
	inline double Project(const FVector2<T>& QueryPoint) const
	{
		return (QueryPoint - Origin).Dot(Direction);
	}

	/**
	 * @return smallest squared distance from line to QueryPoint
	 */
	inline double DistanceSquared(const FVector2<T>& QueryPoint) const
	{
		double t = (QueryPoint - Origin).Dot(Direction);
		FVector2<T> proj = Origin + t * Direction;
		return (proj - QueryPoint).SquaredLength();
	}

	/**
	 * @return +1 if QueryPoint is "right" of line, -1 if "left" or 0 if "on" line (up to given tolerance)
	 */
	inline int WhichSide(const FVector2<T>& QueryPoint, double OnLineTolerance = 0) const
	{
		double x0 = QueryPoint.X - Origin.X;
		double y0 = QueryPoint.Y - Origin.Y;
		double x1 = Direction.X;
		double y1 = Direction.Y;
		double det = x0 * y1 - x1 * y0;
		return (det > OnLineTolerance ? +1 : (det < -OnLineTolerance ? -1 : 0));
	}


	/**
	 * Calculate intersection point between this line and another one
	 * @param OtherLine line to test against
	 * @param IntersectionPointOut intersection point is stored here, if found
	 * @param ParallelDotTolerance tolerance used to determine if lines are parallel (and hence cannot intersect)
	 * @return true if lines intersect and IntersectionPointOut was computed
	 */
	bool IntersectionPoint(const FLine2& OtherLine, FVector2<T>& IntersectionPointOut, double ParallelDotTolerance = TMathUtil<T>::ZeroTolerance) const
	{
		// see IntrFLine2FLine2 for more detailed explanation
		FVector2<T> diff = OtherLine.Origin - Origin;
		double D0DotPerpD1 = Direction.DotPerp(OtherLine.Direction);
		if (TMathUtil<T>::Abs(D0DotPerpD1) > ParallelDotTolerance)                     // FLines intersect in a single point.
		{
			double invD0DotPerpD1 = ((double)1) / D0DotPerpD1;
			double diffDotPerpD1 = diff.DotPerp(OtherLine.Direction);
			T s = diffDotPerpD1 * invD0DotPerpD1;
			IntersectionPointOut = Origin + s * Direction;
			return true;
		}
		// FLines are parallel.
		return false;
	}
};


typedef FLine2<double> FLine2d;
typedef FLine2<float> FLine2f;






/**
 * FLine3 is a three-dimensional infinite line.
 * The line is stored in (Center,Direction) form.
 */
template<typename T>
struct FLine3
{
	/** Origin / Center Point of Line */
	FVector3<T> Origin;

	/** Direction of Line, Normalized */
	FVector3<T> Direction;

	/**
	 * Construct default line along X axis
	 */
	FLine3()
	{
		Origin = FVector3<T>::Zero();
		Direction = FVector3<T>::UnitX();
	}

	/**
	 * Construct line with given Origin and Direction
	 */
	FLine3(const FVector3<T>& OriginIn, const FVector3<T>& DirectionIn)
		: Origin(OriginIn), Direction(DirectionIn)
	{
	}


	/**
	 * @return line between two points
	 */
	static FLine3<T> FromPoints(const FVector3<T>& Point0, const FVector3<T>& Point1)
	{
		return FLine3<T>(Point0, (Point1 - Point0).Normalized());
	}


	/**
	 * @return point on line at given line parameter value (distance along line from origin)
	 */
	inline FVector3<T> PointAt(double LineParameter) const
	{
		return Origin + LineParameter * Direction;
	}


	/**
	 * @return line parameter (ie distance from Origin) at nearest point on line to QueryPoint
	 */
	inline double Project(const FVector3<T>& QueryPoint) const
	{
		return (QueryPoint - Origin).Dot(Direction);
	}

	/**
	 * @return smallest squared distance from line to QueryPoint
	 */
	inline double DistanceSquared(const FVector3<T>& QueryPoint) const
	{
		double t = (QueryPoint - Origin).Dot(Direction);
		FVector3<T> proj = Origin + t * Direction;
		return (proj - QueryPoint).SquaredLength();
	}

};

typedef FLine3<double> FLine3d;
typedef FLine3<float> FLine3f;