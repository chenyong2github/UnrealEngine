// Copyright Epic Games, Inc. All Rights Reserved.

// Port of WildMagic TIntrSegment2Triangle2

#pragma once

#include "VectorTypes.h"
#include "SegmentTypes.h"
#include "TriangleTypes.h"
#include "VectorUtil.h"

#include "Intersection/IntrLine2Triangle2.h"

/**
 * Compute intersection between 2D segment and 2D triangle
 */
template <typename Real>
class TIntrSegment2Triangle2
{
protected:
	// Input
	TSegment2<Real> Segment;
	TTriangle2<Real> Triangle;

public:
	// Output
	int Quantity = 0;
	EIntersectionResult Result = EIntersectionResult::NotComputed;
	EIntersectionType Type = EIntersectionType::Empty;

	bool IsSimpleIntersection()
	{
		return Result == EIntersectionResult::Intersects && Type == EIntersectionType::Point;
	}


	FVector2<Real> Point0;
	FVector2<Real> Point1;
	double Param0;
	double Param1;

	TSegment2<Real> GetSegment() const
	{
		return Segment;
	}
	TTriangle2<Real> GetTriangle() const
	{
		return Triangle;
	}
	void SetSegment(const TSegment2<Real>& SegmentIn)
	{
		Result = EIntersectionResult::NotComputed;
		Segment = SegmentIn;
	}
	void SetTriangle(const TTriangle2<Real>& TriangleIn)
	{
		Result = EIntersectionResult::NotComputed;
		Triangle = TriangleIn;
	}

	TIntrSegment2Triangle2()
	{}
	TIntrSegment2Triangle2(TSegment2<Real> Seg, TTriangle2<Real> Tri) : Segment(Seg), Triangle(Tri)
	{
	}


	TIntrSegment2Triangle2* Compute(Real Tolerance = TMathUtil<Real>::ZeroTolerance)
	{
		Find(Tolerance);
		return this;
	}


	bool Find(Real Tolerance = TMathUtil<Real>::ZeroTolerance)
	{
		if (Result != EIntersectionResult::NotComputed)
		{
			return (Result == EIntersectionResult::Intersects);
		}

		// if either line direction is not a normalized vector, 
		//   results are garbage, so fail query
		if (Segment.Direction.IsNormalized() == false)
		{
			Type = EIntersectionType::Empty;
			Result = EIntersectionResult::InvalidQuery;
			return false;
		}

		FVector3<Real> dist;
		FVector3i sign;
		int positive = 0, negative = 0, zero = 0;
		TIntrLine2Triangle2<Real>::TriangleLineRelations(Segment.Center, Segment.Direction, Triangle,
			dist, sign, positive, negative, zero, Tolerance);

		if (positive == 3 || negative == 3)
		{
			// No intersections.
			Quantity = 0;
			Type = EIntersectionType::Empty;
		}
		else 
		{
			FVector2<Real> param;
			TIntrLine2Triangle2<Real>::GetInterval(Segment.Center, Segment.Direction, Triangle, dist, sign, param);

			TIntersector1<Real> intr(param[0], param[1], -Segment.Extent, +Segment.Extent);
			intr.Find();

			Quantity = intr.NumIntersections;
			if (Quantity == 2)
			{
				// Segment intersection.
				Type = EIntersectionType::Segment;
				Param0 = intr.GetIntersection(0);
				Point0 = Segment.Center + Param0 * Segment.Direction;
				Param1 = intr.GetIntersection(1);
				Point1 = Segment.Center + Param1 * Segment.Direction;
			}
			else if (Quantity == 1) 
			{
				// Point intersection.
				Type = EIntersectionType::Point;
				Param0 = intr.GetIntersection(0);
				Point0 = Segment.Center + Param0 * Segment.Direction;
			}
			else {
				// No intersections.
				Type = EIntersectionType::Empty;
			}
		}

		Result = (Type != EIntersectionType::Empty) ?
			EIntersectionResult::Intersects : EIntersectionResult::NoIntersection;
		return (Result == EIntersectionResult::Intersects);
	}




};

typedef TIntrSegment2Triangle2<float> FIntrSegment2Triangle2f;
typedef TIntrSegment2Triangle2<double> FIntrSegment2Triangle2d;