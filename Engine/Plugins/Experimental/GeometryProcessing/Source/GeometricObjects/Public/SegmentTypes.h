// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// ported from geometry3Sharp Segment2

#pragma once

#include "Math/UnrealMath.h"
#include "VectorTypes.h"
#include "BoxTypes.h"


/*
 * 2D Line Segmented stored as Center point, normalized Direction vector, and scalar Extent
 */
template<typename T>
struct TSegment2
{
public:
	/** Center point of segment */
	FVector2<T> Center;
	/** normalized Direction vector of segment */
	FVector2<T> Direction;
	/** Extent of segment, which is half the total length */
	T Extent;

	/**
	 * Construct a Segment from two Points
	 */
	TSegment2(const FVector2<T>& Point0, const FVector2<T>& Point1)
	{
		// set from endpoints 
		Center = T(.5) * (Point0 + Point1);
		Direction = Point1 - Point0;
		Extent = T(.5) * Direction.Normalize();
	}

	/**
	 * Construct a segment from a Center Point, normalized Direction, and scalar Extent
	 */
	TSegment2(const FVector2<T>& CenterIn, const FVector2<T>& DirectionIn, T ExtentIn)
	{
		Center = CenterIn;
		Direction = DirectionIn; 
		Extent = ExtentIn;
	}



	/** Update the Segment with a new start point */
	inline void SetStartPoint(const FVector2<T>& Point)
	{
		update_from_endpoints(Point, EndPoint());
	}

	/** Update the Segment with a new end point */
	inline void SetEndPoint(const FVector2<T>& Point)
	{
		update_from_endpoints(StartPoint(), Point);
	}

	/** Reverse the segment */
	void Reverse()
	{
		update_from_endpoints(EndPoint(), StartPoint());
	}




	/** @return start point of segment */
	inline FVector2<T> StartPoint() const
	{
		return Center - Extent * Direction;
	}

	/** @return end point of segment */
	inline FVector2<T> EndPoint() const
	{
		return Center + Extent * Direction;
	}


	/** @return the Length of the segment */
	inline T Length() const
	{
		return (T)2 * Extent;
	}

	/** @return first (i == 0) or second (i == 1) endpoint of the Segment  */
	inline FVector2<T> GetPointFromIndex(int i) const
	{
		return (i == 0) ? (Center - Extent * Direction) : (Center + Extent * Direction);
	}

	/**
	 * @return point on segment at given (signed) Distance from the segment Origin
	 */
	inline FVector2<T> PointAt(T DistanceParameter) const
	{
		return Center + DistanceParameter * Direction;
	}

	/**
	 * @param UnitParameter value in range [0,1]
	 * @return point on segment at that linearly interpolates between start and end based on Unit Parameter
	 */
	inline FVector2<T> PointBetween(T UnitParameter) const
	{
		return Center + ((T)2 * UnitParameter - (T)1) * Extent * Direction;
	}

	/**
	 * @return minimum squared distance from Point to segment
	 */
	inline T DistanceSquared(const FVector2<T>& Point) const
	{
		T DistParameter;
		return DistanceSquared(Point, DistParameter);
	}



	/**
	 * @param Point query point
	 * @param DistParameterOut calculated distance parameter in range [-Extent,Extent]
	 * @return minimum squared distance from Point to Segment
	 */	
	T DistanceSquared(const FVector2<T>& Point, T& DistParameterOut) const
	{
		DistParameterOut = (Point - Center).Dot(Direction);
		if (DistParameterOut >= Extent)
		{
			DistParameterOut = Extent;
			return Point.DistanceSquared(EndPoint());
		}
		else if (DistParameterOut <= -Extent)
		{
			DistParameterOut = -Extent;
			return Point.DistanceSquared(StartPoint());
		}
		FVector2<T> ProjectedPt = Center + DistParameterOut * Direction;
		return ProjectedPt.DistanceSquared(Point);
	}


	/**
	 * @return nearest point on segment to QueryPoint
	 */
	inline FVector2<T> NearestPoint(const FVector2<T>& QueryPoint) const
	{
		T t = (QueryPoint - Center).Dot(Direction);
		if (t >= Extent)
		{
			return EndPoint();
		}
		if (t <= -Extent)
		{
			return StartPoint();
		}
		return Center + t * Direction;
	}


	/**
	 * @return scalar projection of QueryPoint onto line of Segment (not clamped to Extents)
	 */
	inline T Project(const FVector2<T>& QueryPoint) const
	{
		return (QueryPoint - Center).Dot(Direction);
	}


	/**
	 * @return scalar projection of QueryPoint onto line of Segment, mapped to [0,1] range along segment
	 */
	inline T ProjectUnitRange(const FVector2<T>& QueryPoint) const
	{
		T ProjT = (QueryPoint - Center).Dot(Direction);
		T Alpha = ((ProjT / Extent) + (T)1) * (T)0.5;
		return TMathUtil<T>::Clamp(Alpha, (T)0, (T)1);
	}



	/**
	 * Determine which side of the segment the query point lies on
	 * @param QueryPoint test point
	 * @param Tolerance tolerance band in which we return 0
	 * @return +1 if point is to right of line, -1 if left, and 0 if on line or within tolerance band
	 */
	int WhichSide(const FVector2<T>& QueryPoint, T Tolerance = 0)
	{
		// [TODO] subtract Center from test?
		FVector2<T> EndPt = Center + Extent * Direction;
		FVector2<T> StartPt = Center - Extent * Direction;
		T det = -FVector2<T>::Orient(EndPt, StartPt, QueryPoint);
		return (det > Tolerance ? +1 : (det < -Tolerance ? -1 : 0));
	}


	/**
	 * Test if this segment intersects with OtherSegment. Returns true for parallel-line overlaps. Returns same result as IntrSegment2Segment2
	 * @param OtherSegment segment to test against
	 * @param DotThresh dot-product tolerance used to determine if segments are parallel
	 * @param IntervalThresh distance tolerance used to allow slighly-not-touching segments to be considered overlapping
	 * @return true if segments intersect
	 */
	bool Intersects(const TSegment2<T>& OtherSegment, T DotThresh = TMathUtil<T>::Epsilon, T IntervalThresh = 0) const
	{
		// see IntrLine2Line2 and IntrSegment2Segment2 for details on this code

		FVector2<T> diff = OtherSegment.Center - Center;
		T D0DotPerpD1 = Direction.DotPerp(OtherSegment.Direction);
		if (TMathUtil<T>::Abs(D0DotPerpD1) > DotThresh)      // Lines intersect in a single point.
		{
			T invD0DotPerpD1 = ((T)1) / D0DotPerpD1;
			T diffDotPerpD0 = diff.DotPerp(Direction);
			T diffDotPerpD1 = diff.DotPerp(OtherSegment.Direction);
			T s = diffDotPerpD1 * invD0DotPerpD1;
			T s2 = diffDotPerpD0 * invD0DotPerpD1;
			return TMathUtil<T>::Abs(s) <= (Extent + IntervalThresh)
				&& TMathUtil<T>::Abs(s2) <= (OtherSegment.Extent + IntervalThresh);
		}

		// Lines are parallel.
		diff.Normalize();
		T diffNDotPerpD1 = diff.DotPerp(OtherSegment.Direction);
		if (TMathUtil<T>::Abs(diffNDotPerpD1) <= DotThresh)
		{
			// Compute the location of OtherSegment endpoints relative to our Segment
			diff = OtherSegment.Center - Center;
			T t1 = Direction.Dot(diff);
			T tmin = t1 - OtherSegment.Extent;
			T tmax = t1 + OtherSegment.Extent;
			TInterval1<T> extents(-Extent, Extent);
			if (extents.Overlaps(TInterval1<T>(tmin, tmax)))
			{
				return true;
			}
			return false;
		}

		// lines are parallel but not collinear
		return false;
	}





	// 2D segment utility functions


	/**
	 * Calculate distance from QueryPoint to segment (StartPt,EndPt)
	 */
	static T FastDistanceSquared(const FVector2<T>& StartPt, const FVector2<T>& EndPt, const FVector2<T>& QueryPt, T Tolerance = TMathUtil<T>::Epsilon)
	{
		T vx = EndPt.X - StartPt.X, vy = EndPt.Y - StartPt.Y;
		T len2 = vx * vx + vy * vy;
		T dx = QueryPt.X - StartPt.X, dy = QueryPt.Y - StartPt.Y;
		//if (len2 < 1e-13) 
		if (len2 < Tolerance)
		{
			return dx * dx + dy * dy;
		}
		T t = (dx*vx + dy * vy);
		if (t <= 0) 
		{
			return dx * dx + dy * dy;
		}
		else if (t >= len2) 
		{
			dx = QueryPt.X - EndPt.X; 
			dy = QueryPt.Y - EndPt.Y;
			return dx * dx + dy * dy;
		}
		dx = QueryPt.X - (StartPt.X + ((t * vx) / len2));
		dy = QueryPt.Y - (StartPt.Y + ((t * vy) / len2));
		return dx * dx + dy * dy;
	}


	/**
	 * Determine which side of the segment the query point lies on
	 * @param StartPt first point of Segment
	 * @param EndPt second point of Segment
	 * @param QueryPoint test point
	 * @param Tolerance tolerance band in which we return 0
	 * @return +1 if point is to right of line, -1 if left, and 0 if on line or within tolerance band
	 */
	static int WhichSide(const FVector2<T>& StartPt, const FVector2<T>& EndPt, const FVector2<T>& QueryPt, T Tolerance = (T)0)
	{
		T det = -FVector2<T>::Orient(StartPt, EndPt, QueryPt);
		return (det > Tolerance ? +1 : (det < -Tolerance ? -1 : 0));
	}





protected:

	// update segment based on new endpoints
	inline void update_from_endpoints(const FVector2<T>& p0, const FVector2<T>& p1)
	{
		Center = 0.5 * (p0 + p1);
		Direction = p1 - p0;
		Extent = 0.5 * Direction.Normalize();
	}

};
typedef TSegment2<float> FSegment2f;
typedef TSegment2<double> FSegment2d;








/*
 * 3D Line Segmented stored as Center point, normalized Direction vector, and scalar Extent
 */
template<typename T>
struct TSegment3
{
public:
	/** Center point of segment */
	FVector3<T> Center;
	/** normalized Direction vector of segment */
	FVector3<T> Direction;
	/** Extent of segment, which is half the total length */
	T Extent;

	/**
	 * Construct a Segment from two Points
	 */
	TSegment3(const FVector3<T>& Point0, const FVector3<T>& Point1)
	{
		// set from endpoints 
		Center = T(.5) * (Point0 + Point1);
		Direction = Point1 - Point0;
		Extent = T(.5) * Direction.Normalize();
	}

	/**
	 * Construct a segment from a Center Point, normalized Direction, and scalar Extent
	 */
	TSegment3(const FVector3<T>& CenterIn, const FVector3<T>& DirectionIn, T ExtentIn)
	{
		Center = CenterIn;
		Direction = DirectionIn;
		Extent = ExtentIn;
	}



	/** Update the Segment with a new start point */
	inline void SetStartPoint(const FVector3<T>& Point)
	{
		update_from_endpoints(Point, EndPoint());
	}

	/** Update the Segment with a new end point */
	inline void SetEndPoint(const FVector3<T>& Point)
	{
		update_from_endpoints(StartPoint(), Point);
	}

	/** Reverse the segment */
	void Reverse()
	{
		update_from_endpoints(EndPoint(), StartPoint());
	}




	/** @return start point of segment */
	inline FVector3<T> StartPoint() const
	{
		return Center - Extent * Direction;
	}

	/** @return end point of segment */
	inline FVector3<T> EndPoint() const
	{
		return Center + Extent * Direction;
	}


	/** @return the Length of the segment */
	inline T Length() const
	{
		return (T)2 * Extent;
	}

	/** @return first (i == 0) or second (i == 1) endpoint of the Segment  */
	inline FVector3<T> GetPointFromIndex(int i) const
	{
		return (i == 0) ? (Center - Extent * Direction) : (Center + Extent * Direction);
	}

	/**
	 * @return point on segment at given (signed) Distance from the segment Origin
	 */
	inline FVector3<T> PointAt(T DistanceParameter) const
	{
		return Center + DistanceParameter * Direction;
	}

	/**
	 * @param UnitParameter value in range [0,1]
	 * @return point on segment at that linearly interpolates between start and end based on Unit Parameter
	 */
	inline FVector3<T> PointBetween(T UnitParameter) const
	{
		return Center + ((T)2 * UnitParameter - (T)1) * Extent * Direction;
	}

	/**
	 * @return minimum squared distance from Point to segment
	 */
	inline T DistanceSquared(const FVector3<T>& Point) const
	{
		T DistParameter;
		return DistanceSquared(Point, DistParameter);
	}



	/**
	 * @param Point query point
	 * @param DistParameterOut calculated distance parameter in range [-Extent,Extent]
	 * @return minimum squared distance from Point to Segment
	 */
	T DistanceSquared(const FVector3<T>& Point, T& DistParameterOut) const
	{
		DistParameterOut = (Point - Center).Dot(Direction);
		if (DistParameterOut >= Extent)
		{
			DistParameterOut = Extent;
			return Point.DistanceSquared(EndPoint());
		}
		else if (DistParameterOut <= -Extent)
		{
			DistParameterOut = -Extent;
			return Point.DistanceSquared(StartPoint());
		}
		FVector3<T> ProjectedPt = Center + DistParameterOut * Direction;
		return ProjectedPt.DistanceSquared(Point);
	}


	/**
	 * @return nearest point on segment to QueryPoint
	 */
	inline FVector3<T> NearestPoint(const FVector3<T>& QueryPoint) const
	{
		T t = (QueryPoint - Center).Dot(Direction);
		if (t >= Extent)
		{
			return EndPoint();
		}
		if (t <= -Extent)
		{
			return StartPoint();
		}
		return Center + t * Direction;
	}


	/**
	 * @return scalar projection of QueryPoint onto line of Segment (not clamped to Extents)
	 */
	inline T Project(const FVector3<T>& QueryPoint) const
	{
		return (QueryPoint - Center).Dot(Direction);
	}


	/**
	 * @return scalar projection of QueryPoint onto line of Segment, mapped to [0,1] range along segment
	 */
	inline T ProjectUnitRange(const FVector3<T>& QueryPoint) const
	{
		T ProjT = (QueryPoint - Center).Dot(Direction);
		T Alpha = ((ProjT / Extent) + (T)1) * (T)0.5;
		return TMathUtil<T>::Clamp(Alpha, (T)0, (T)1);
	}


protected:

	// update segment based on new endpoints
	inline void update_from_endpoints(const FVector3<T>& p0, const FVector3<T>& p1)
	{
		Center = 0.5 * (p0 + p1);
		Direction = p1 - p0;
		Extent = 0.5 * Direction.Normalize();
	}

};
typedef TSegment3<float> FSegment3f;
typedef TSegment3<double> FSegment3d;
