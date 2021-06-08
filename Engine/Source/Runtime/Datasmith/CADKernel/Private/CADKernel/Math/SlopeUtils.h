// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CADKernel/Core/Types.h"
#include "CADKernel/Math/Point.h"

namespace CADKernel
{

	/**
	 * Fast angle approximation by a "slope" :
	 * This method is used to compute an approximation of the angle between the input segment defined by two points and [0, u) axis.
	 * The return value is a real in the interval [0, 8] for an angle in the interval [0, 2Pi]
	 * Warning, it's only an approximation... The conversion is not linear but the error is small near the integer value of slop (0, 1, 2, 3, ...8)
	 * 
	 * This approximation is very good when only comparison of angles is needed.
	 * This method is not adapted to compute angle
	 *
	 * To compute an angle value between two segments, the call of acos (and asin for an oriented angle) is necessary while with this approximation, only a division is useful.
	 * 
	 * [0 - 2Pi] is divide into 8 angular sector i.e. [0, Pi/4] = [0,1], [Pi/4, Pi/2] = [1,2], ...
	 * 
	 * @return a slop between [0, 8] i.e. an equivalent angle between [0, 2Pi]
	 *
	 * Angle (Degree) to Slop
	 *		  0   = 0
	 *		  7.  = 0.125
	 *		 14.  = 0.25
	 *		 30   = 0.5
	 *		 36.8 = 0.75 
	 *		 45   = 1
	 *		 53.2 = 1.25
	 *		 60   = 1.5
	 *		 76.  = 1.75
	 *		 90   = 2
	 *		135   = 3
	 *		180   = 4
	 *		360   = 8
	 */
	inline double ComputeSlope(const FPoint2D& StartPoint, const FPoint2D& EndPoint)
	{
		double DeltaU = EndPoint.U - StartPoint.U;
		double DeltaV = EndPoint.V - StartPoint.V;
		double Delta;

		if (FMath::Abs(DeltaU) < SMALL_NUMBER && FMath::Abs(DeltaV) < SMALL_NUMBER)
		{
			return 0;
		}

		if (DeltaU > SMALL_NUMBER)
		{
			if (DeltaV > SMALL_NUMBER)
			{
				if (DeltaU > DeltaV)
				{
					Delta = DeltaV / DeltaU;
				}
				else
				{
					Delta = 2 - DeltaU / DeltaV;
				}
			}
			else
			{
				if (DeltaU > -DeltaV)
				{
					Delta = 8 + DeltaV / DeltaU;
				}
				else
				{
					Delta = 6 - DeltaU / DeltaV; // deltaU/deltaV <0
				}
			}
		}
		else
		{
			if (DeltaV > SMALL_NUMBER)
			{
				if (-DeltaU > DeltaV)
				{
					Delta = 4 + DeltaV / DeltaU;
				}
				else
				{
					Delta = 2 - DeltaU / DeltaV;
				}
			}
			else
			{
				if (-DeltaU > -DeltaV)
				{
					Delta = 4 + DeltaV / DeltaU;
				}
				else
				{
					Delta = 6 - DeltaU / DeltaV;
				}
			}
		}

		return Delta;
	}


	/**
	 * Compute the oriented slope of a segment according to a reference slope
	 * This method is used to compute an approximation of the angle between two segments in 2D.
	 * return a slop between [0, 8] i.e. an equivalent angle between [0, 2Pi]
 	 */
	inline double ComputePositiveSlope(const FPoint2D& StartPoint, const FPoint2D& EndPoint, double ReferenceSlope)
	{
		double Slope = ComputeSlope(StartPoint, EndPoint);
		Slope -= ReferenceSlope;
		if (Slope < 0) Slope += 8;
		return Slope;
	}

	/**
	 * Compute the positive slope of a segment according to a reference slope
	 * @return a slop between [0, 8] i.e. an equivalent angle between [0, 2Pi]
	 */
	inline double ComputePositiveSlope(const FPoint2D& StartPoint, const FPoint2D& EndPoint1, const FPoint2D& EndPoint2)
	{
		double ReferenceSlope = ComputeSlope(StartPoint, EndPoint1);
		double Slope = ComputeSlope(StartPoint, EndPoint2);
		Slope -= ReferenceSlope;
		if (Slope < 0) Slope += 8;
		return Slope;
	}

	/**
	 * Transform a positive slope into an oriented slope [-4, 4] i.e. an equivalent angle between [-Pi, Pi]
	 * @return a slop between [-4, 4]
	 */
	inline double TransformIntoOrientedSlope(double Slope)
	{
		if (Slope > 4.) Slope -= 8;
		if (Slope < -4.) Slope += 8;
		return Slope;
	}

	/**
	 * Compute the oriented slope of a segment according to a reference slope
	 * @return a slop between [-4, 4] i.e. an equivalent angle between [-Pi, Pi]
	 */
	inline double ComputeOrientedSlope(const FPoint2D& StartPoint, const FPoint2D& EndPoint, double ReferenceSlope)
	{
		return TransformIntoOrientedSlope(ComputePositiveSlope(StartPoint, EndPoint, ReferenceSlope));
	}

	/**
	 * return a slop between [0, 4] i.e. an angle between [0, Pi]
	 */
	inline double ComputeUnorientedSlope(const FPoint2D& StartPoint, const FPoint2D& EndPoint, double ReferenceSlope)
	{
		return FMath::Abs(ComputeOrientedSlope(StartPoint, EndPoint, ReferenceSlope));
	}

	/**
	 *                         P1
	 *          inside        /
	 *                       /   inside
	 *                      /
	 *    A -------------- B --------------- C 
	 *                      \
	 *           Outside     \  Outside
	 *                        \
	 *                         P2
	 *
	 * Return true if the segment BP is inside the sector defined the half-lines [BA) and [BC) in the counterclockwise.
	 * Return false if ABP angle or PBC angle is too flat (smaller than FlatAngle)
	 */
	inline bool IsPointPBeInsideSectorABC(const FPoint2D& PointA, const FPoint2D& PointB, const FPoint2D& PointC, const FPoint2D& PointP, const double FlatAngle)
	 {
		 double SlopWithNextBoundary = ComputeSlope(PointB, PointC);
		 double BoundaryDeltaSlope = ComputePositiveSlope(PointB, PointA, SlopWithNextBoundary);
		 double SegmentSlope = ComputePositiveSlope(PointB, PointP, SlopWithNextBoundary);
		 if (SegmentSlope < FlatAngle || SegmentSlope + FlatAngle > BoundaryDeltaSlope)
		 {
			 return false;
		 }
		 return true;
	 }


} // namespace CADKernel	
