// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"

#include "CADKernel/Geo/GeoEnum.h"
#include "CADKernel/Math/Aabb.h"
#include "CADKernel/Math/MathConst.h"
#include "CADKernel/Math/MatrixH.h"
#include "CADKernel/Math/Point.h"

namespace CADKernel
{
	enum EPolygonSide : uint8
	{
		Side01 = 0,
		Side12,
		Side20,
		Side23,
		Side30,
	};
	
	template<class PointType>
	struct CADKERNEL_API TSegment
	{
		const PointType& Point0;
		const PointType& Point1;

		TSegment(const PointType& InPoint0, const PointType& InPoint1)
			: Point0(InPoint0)
			, Point1(InPoint1)
		{
		}

		constexpr const PointType& operator[](int32 Index) const
		{
			ensureCADKernel(Index < 2);
			switch (Index)
			{
			case 0:
				return Point0;
			case 1:
				return Point1;
			default:
				return PointType::ZeroPoint;
			}
		}
	};
	
	template<class PointType>
	struct CADKERNEL_API TTriangle
	{
		const PointType& Point0;
		const PointType& Point1;
		const PointType& Point2;

		TTriangle(const PointType& InPoint0, const PointType& InPoint1, const PointType& InPoint2)
			: Point0(InPoint0)
			, Point1(InPoint1)
			, Point2(InPoint2)
		{
		}

		constexpr const PointType& operator[](int32 Index) const 
		{
			ensureCADKernel(Index < 3);
			switch (Index)
			{
				case 0:
					return Point0;
				case 1:
					return Point1;
				case 2:
					return Point2;
				default:
					return PointType::ZeroPoint;
			}
		}

		virtual inline PointType ProjectPoint(const PointType& InPoint, FPoint2D& OutCoordinate)
		{
			PointType Segment01 = Point1 - Point0;
			PointType Segment02 = Point2 - Point0;
			double SquareLength01 = Segment01.SquareLength();
			double SquareLength02 = Segment02.SquareLength();
			double Seg01Seg02 = Segment01 * Segment02;
			double Det = SquareLength01 * SquareLength02 - FMath::Square(Seg01Seg02);

			int32 SideIndex;
			// If the 3 points are aligned
			if (FMath::IsNearlyZero(Det))
			{
				double MaxLength = SquareLength01;
				SideIndex = Side01;
				if (SquareLength02 > MaxLength)
				{
					MaxLength = SquareLength02;
					SideIndex = Side20;
				}

				PointType Segment12 = Point2 - Point1;
				if (Segment12.SquareLength() > MaxLength)
				{
					SideIndex = Side12;
				}
			}
			else
			{
				// Resolve
				PointType Segment1Point = InPoint - Point0;
				double Segment1PointSegment01 = Segment1Point * Segment01;
				double Segment1PointSegment02 = Segment1Point * Segment02;

				OutCoordinate.U = ((Segment1PointSegment01 * SquareLength02) - (Segment1PointSegment02 * Seg01Seg02)) / Det;
				OutCoordinate.V = ((Segment1PointSegment02 * SquareLength01) - (Segment1PointSegment02 * Seg01Seg02)) / Det;

				// tester la solution pour choisir parmi 4 possibilites
				if (OutCoordinate.U < 0.0)
				{
					// the project point is on the segment 02
					SideIndex = Side20;
				}
				else if (OutCoordinate.V < 0.0)
				{
					// the project point is on the segment 01
					SideIndex = Side01;
				}
				else if ((OutCoordinate.U + OutCoordinate.V) > 1.0)
				{
					// the project point is on the segment 12
					SideIndex = Side12;
				}
				else {
					// the project point is inside the Segment
					Segment01 = Segment01 * OutCoordinate.U;
					Segment02 = Segment02 * OutCoordinate.V;
					PointType ProjectedPoint = Segment01 + Segment02;
					ProjectedPoint = ProjectedPoint + Point0;
					return ProjectedPoint;
				}
			}

			// projects the point on the nearest side
			PointType ProjectedPoint;
			double SegmentCoordinate;
			switch (SideIndex)
			{
			case Side01:
				ProjectedPoint = ProjectPointOnSegment(InPoint, Point0, Point1, SegmentCoordinate);
				OutCoordinate.U = SegmentCoordinate;
				OutCoordinate.V = 0.0;
				break;
			case Side20:
				ProjectedPoint = ProjectPointOnSegment(InPoint, Point0, Point2, SegmentCoordinate);
				OutCoordinate.U = 0.0;
				OutCoordinate.V = SegmentCoordinate;
				break;
			case Side12:
				ProjectedPoint = ProjectPointOnSegment(InPoint, Point1, Point2, SegmentCoordinate);
				OutCoordinate.U = 1.0 - SegmentCoordinate;
				OutCoordinate.V = SegmentCoordinate;
				break;
			}
			return ProjectedPoint;
		}

		virtual PointType CircumCircleCenter() const  = 0;
	};

	struct CADKERNEL_API FTriangle : public TTriangle<FPoint>
	{
		FTriangle(const FPoint& InPoint0, const FPoint& InPoint1, const FPoint& InPoint2)
			: TTriangle<FPoint>(InPoint0, InPoint1, InPoint2)
		{
		}

		virtual FPoint ComputeNormal() const
		{
			FPoint Normal = (Point1 - Point0) ^ (Point2 - Point0);
			Normal.Normalize();
			return Normal;
		}

		virtual FPoint CircumCircleCenter() const override
		{
			FMatrixH Matrix;
			FPoint Trans;

			FPoint TriangleNormal = ComputeNormal();
			Matrix(0, 0) = TriangleNormal[0];
			Matrix(1, 0) = TriangleNormal[1];
			Matrix(2, 0) = TriangleNormal[2];

			Trans[0] = TriangleNormal * Point0;

			FPoint Segment01 = Point1 - Point0;
			Segment01.Normalize();
			Matrix(0, 1) = Segment01[0];
			Matrix(1, 1) = Segment01[1];
			Matrix(2, 1) = Segment01[2];

			FPoint Passage = (Point1 + Point0) / 2;

			Trans[1] = Segment01 * Passage;

			FPoint Segment02 = Point2 - Point0;
			Segment02.Normalize();
			Matrix(0, 2) = Segment02[0];
			Matrix(1, 2) = Segment02[1];
			Matrix(2, 2) = Segment02[2];

			Passage = (Point2 + Point0) / 2;
			Trans[2] = Segment02 * Passage;

			Matrix.Inverse();

			FPoint Center = Matrix * Trans;
			return Center;
		}
	};

	struct CADKERNEL_API FTriangle2D : public TTriangle<FPoint2D>
	{
		FTriangle2D(const FPoint2D& InPoint0, const FPoint2D& InPoint1, const FPoint2D& InPoint2)
			: TTriangle<FPoint2D>(InPoint0, InPoint1, InPoint2)
		{
		}

		/**
		 * https://en.wikipedia.org/wiki/Circumscribed_circle#Cartesian_coordinates_2
		 * With A = (0, 0)
		 */
		virtual FPoint2D CircumCircleCenter() const override
		{
			FPoint2D A = FPoint2D::ZeroPoint;
			FPoint2D B = Point1 - Point0;
			FPoint2D C = Point2 - Point0;

			// D = 2(BuCv - BvCu)
			double D = 2. * B ^ C;
			if (FMath::IsNearlyZero(D, SMALL_NUMBER_SQUARE))
			{
				ensureCADKernel(false);
			}

			// CenterU  = 1/D * (Cv.|B|.|B| - By.|C|.|C|) = 1/D * CBv ^ SquareNorms 
			// CenterV  = 1/D * (Bu.|B|.|B| - Cu.|C|.|C|) = -1/D * SquareNorms ^ CBu 
			// with CBu = (Cu, Bu), CBv = (Cv, Bv), SquareNorms = (|B|.|B|, |C|.|C|)
			FPoint2D CBu(C.U, B.U);
			FPoint2D CBv(C.V, B.V);
			FPoint2D SquareNorms(B.SquareLength(), C.SquareLength());

			double CenterU = CBu ^ SquareNorms / D;
			double CenterV = SquareNorms ^ CBu / D;
			return FPoint2D(CenterU, CenterV) + Point0;
		}

		/**
		 * Based on 
		 * https://en.wikipedia.org/wiki/Circumscribed_circle#Cartesian_coordinates_2
		 * With A = (0, 0)
		 */
		FPoint2D CircumCircleCenterWithSquareRadius(double& SquareRadius) const
		{
			FPoint2D B = Point1 - Point0;
			FPoint2D C = Point2 - Point0;

			// D = 2(BuCv - BvCu)
			double D = 2. * B ^ C;
			if (FMath::IsNearlyZero(D, SMALL_NUMBER_SQUARE))
			{
				SquareRadius = -1;
				return FPoint2D::ZeroPoint;
			}

			// CenterU  = 1/D * (Cv.|B|.|B| - Bv.|C|.|C|) = 1/D * CBv ^ SquareNorms 
			// CenterV  = 1/D * (Bu.|C|.|C| - Cu.|B|.|B|) = 1/D * SquareNorms ^ CBu  
			// with CBu = (Cu, Bu), CBv = (Cv, Bv), SquareNorms = (|B|.|B|, |C|.|C|)
			FPoint2D CBu(C.U, B.U);
			FPoint2D CBv(C.V, B.V);
			FPoint2D SquareNorms(C.SquareLength(), B.SquareLength());

			double CenterU = CBv ^ SquareNorms / D;
			double CenterV = SquareNorms ^ CBu / D;
			FPoint2D Center(CenterU, CenterV);
			SquareRadius = Center.SquareLength();
			return FPoint2D(CenterU, CenterV) + Point0;
		}
	};


	template<class PointType>
	struct CADKERNEL_API TQuadrangle
	{
		const PointType& Point0;
		const PointType& Point1;
		const PointType& Point2;
		const PointType& Point3;

		TQuadrangle(const PointType& InPoint0, const PointType& InPoint1, const PointType& InPoint2, const PointType& InPoint3)
			: Point0(InPoint0)
			, Point1(InPoint1)
			, Point2(InPoint2)
			, Point3(InPoint3)
		{
		}

		constexpr const PointType& operator[](int32 Index) const
		{
			ensureCADKernel(Index < 4);
			switch (Index)
			{
			case 0:
				return Point0;
			case 1:
				return Point1;
			case 2:
				return Point2;
			case 3:
				return Point3;
			default:
				return PointType::ZeroPoint;
			}
		}

		inline FPoint ComputeNormal(const TQuadrangle<FPoint>& InQuadrangle) const
		{
			FPoint Normal = (InQuadrangle[1] - InQuadrangle[0]) ^ (InQuadrangle[2] - InQuadrangle[0]);
			Normal += (InQuadrangle[2] - InQuadrangle[0]) ^ (InQuadrangle[3] - InQuadrangle[0]);
			Normal += (InQuadrangle[1] - InQuadrangle[0]) ^ (InQuadrangle[3] - InQuadrangle[0]);
			Normal += (InQuadrangle[1] - InQuadrangle[3]) ^ (InQuadrangle[2] - InQuadrangle[3]);
			Normal.Normalize();
			return Normal;
		}

		inline PointType  ProjectPoint(const PointType& InPoint, FPoint2D& OutCoordinate)
		{
			FPoint2D Coordinate013;
			TTriangle<PointType> Triangle013(Point0, Point1, Point3);
			PointType Projection013 = Triangle013.ProjectPoint(InPoint, Coordinate013);

			FPoint2D Coordinate231;
			TTriangle<PointType> Triangle231(Point2, Point3, Point1);
			PointType Projection231 = Triangle231.ProjectPoint(InPoint, Coordinate231);

			double DistanceTo013 = Projection013.Distance(InPoint);
			double DistanceTo231 = Projection231.Distance(InPoint);
			if (DistanceTo013 < DistanceTo231)
			{
				OutCoordinate = Coordinate013;
				return Projection013;
			}
			else
			{
				OutCoordinate = { 1.0 , 1.0 };
				OutCoordinate -= Coordinate231;
				return Projection231;
			}
		}

	};

	inline FPoint ProjectPointOnPlane(const FPoint& Point, const FPoint& Origin, const FPoint& InNormal, double& OutDistance)
	{
		FPoint Normal = InNormal;
		ensureCADKernel(!FMath::IsNearlyZero(Normal.Length()));
		Normal.Normalize();

		FPoint OP = Point - Origin;
		OutDistance = OP * Normal;

		return Point - (Normal * OutDistance);
	}

	/**
	 * @return the distance between the point and the segment. If the projection of the point on the segment
	 * is not inside it, return the distance of the point to nearest the segment extremity
	 */
	template<class PointType>
	inline double DistanceOfPointToSegment(const PointType& Point, const PointType& SegmentPoint1, const PointType& SegmentPoint2)
	{
		double Coordinate;
		return ProjectPointOnSegment(Point, SegmentPoint1, SegmentPoint2, Coordinate, /*bRestrictCoodinateToInside*/ true).Distance(Point);
	}

	/**
	 * @return the distance between the point and the line i.e. the distance between the point and its projection on the line 
	 */
	template<class PointType>
	inline double DistanceOfPointToLine(const PointType& Point, const PointType& LinePoint1, const PointType& LineDirection)
	{
		double Coordinate;
		return ProjectPointOnSegment(Point, LinePoint1, LinePoint1 + LineDirection, Coordinate, /*bRestrictCoodinateToInside*/ false).Distance(Point);
	}

	CADKERNEL_API double ComputeCurvature(const FPoint& Gradient, const FPoint& Laplacian);
	CADKERNEL_API double ComputeCurvature(const FPoint& normal, const FPoint& Gradient, const FPoint& Laplacian);

	/**
	 * @param OutCoordinate: the coordinate of the projected point in the segment AB (coodinate of A = 0 and of B = 1)
	 * @return Projected point
	 */
	template<class PointType>
	inline PointType ProjectPointOnSegment(const PointType& Point, const PointType& InSegmentA, const PointType& InSegmentB, double& OutCoordinate, bool bRestrictCoodinateToInside = true)
	{
		PointType Segment = InSegmentB - InSegmentA;

		double SquareLength = Segment * Segment;

		if (SquareLength <= 0.0)
		{
			OutCoordinate = 0.0;
			return InSegmentA;
		}
		else
		{
			PointType APoint = Point - InSegmentA;
			OutCoordinate = (APoint * Segment) / SquareLength;

			if (bRestrictCoodinateToInside)
			{
				if (OutCoordinate < 0.0)
				{
					OutCoordinate = 0.0;
					return InSegmentA;
				}

				if (OutCoordinate > 1.0)
				{
					OutCoordinate = 1.0;
					return InSegmentB;
				}
			}

			PointType ProjectedPoint = Segment * OutCoordinate;
			ProjectedPoint += InSegmentA;
			return ProjectedPoint;
		}
	}

	/**
	 * @return Coordinate of the projected point in the segment AB (coordinate of A = 0 and of B = 1)
	 */
	template<class PointType>
	inline double CoordinateOfProjectedPointOnSegment(const PointType& Point, const PointType& InSegmentA, const PointType& InSegmentB, bool bRestrictCoodinateToInside = true)
	{
		PointType Segment = InSegmentB - InSegmentA;

		double SquareLength = FMath::Square(Segment);

		if (SquareLength <= 0.0)
		{
			return 0.0;
		}
		else
		{
			PointType APoint = Point - InSegmentA;
			double Coordinate = (APoint * Segment) / SquareLength;

			if (bRestrictCoodinateToInside)
			{
				if (Coordinate < 0.0)
				{
					return 0.0;
				}

				if (Coordinate > 1.0)
				{
					return 1.0;
				}
			}

			return Coordinate;
		}
	}

	CADKERNEL_API void FindLoopIntersectionsWithIso(const EIso Iso, const double IsoParameter, const TArray<TArray<FPoint2D>>& Loops, TArray<double>& OutIntersections);

	/**
	 * Similar as IntersectSegments2D but do not check intersection if both segment are carried by the same line.
	 * Must be done before (with BBox comparison for example)
	 * This method is 50% faster than IntersectSegments2D even if
	 */
	inline bool FastIntersectSegments2D(const TSegment<FPoint2D>& SegmentAB, const TSegment<FPoint2D>& SegmentCD)
	{
		constexpr const double Min = -SMALL_NUMBER;
		constexpr const double Max = 1. + SMALL_NUMBER;

		FPoint2D AB = SegmentAB[1] - SegmentAB[0];
		FPoint2D CD = SegmentCD[1] - SegmentCD[0];
		FPoint2D CA = SegmentAB[0] - SegmentCD[0];

		double ParallelCoef = CD ^ AB;
		double ABIntersectionCoordinate = (CA ^ CD) / ParallelCoef;
		double CDIntersectionCoordinate = (CA ^ AB) / ParallelCoef;

		if (FMath::IsNearlyZero(ParallelCoef))
		{
			ParallelCoef = CA ^ AB;
			if (!FMath::IsNearlyZero(ParallelCoef))
			{
				return false;
			}
			return true;
		}

		return (ABIntersectionCoordinate <= Max && ABIntersectionCoordinate >= Min && CDIntersectionCoordinate <= Max && CDIntersectionCoordinate >= Min);
	}

	/**
	 * Similar as FastIntersectSegments2D but check intersection if both segment are carried by the same line.
	 * This method is 50% slower than FastIntersectSegments2D even if the segments tested are never carried by the same line
	 */
	CADKERNEL_API bool IntersectSegments2D(const TSegment<FPoint2D>&SegmentAB, const TSegment<FPoint2D>&SegmentCD);

} // namespace CADKernel

