// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"
#include "CADKernel/Math/Boundary.h"
#include "CADKernel/Math/Geometry.h"
#include "CADKernel/Math/Point.h"
#include "CADKernel/Geo/GeoEnum.h"
#include "CADKernel/Geo/GeoPoint.h"
#include "CADKernel/Utils/IndexOfCoordinateFinder.h"

#include "Algo/ForEach.h"
#include "Algo/Reverse.h"

namespace CADKernel
{
	namespace PolylineTools
	{
		inline bool IsDichotomyToBePreferred(int32 InPolylineSize, int32 ResultSize)
		{
			float MeanLinearIteration = (float)InPolylineSize / (float)ResultSize;
			float MaxDichotomyIteration = FMath::Log2((float)InPolylineSize);

			if (MeanLinearIteration > MaxDichotomyIteration)
			{
				return true;
			}
			return false;
		}

		template<typename PointType>
		inline PointType LinearInterpolation(const TArray<PointType>& Array, const int32 Index, const double Coordinate)
		{
			ensureCADKernel(Index + 1 < Array.Num());
			return Array[Index] + (Array[Index + 1] - Array[Index]) * Coordinate;
		}

		inline double SectionCoordinate(const TArray<double>& Array, const int32 Index, const double Coordinate)
		{
			ensureCADKernel(Index + 1 < Array.Num());
			const double DeltaU = Array[Index + 1] - Array[Index];
			if (FMath::IsNearlyZero(DeltaU))
			{
				return 0;
			}
			return (Coordinate - Array[Index]) / DeltaU;
		}

		/**
		 * Progressively deforms a polyline (or a control polygon) so that its end is in the desired position 
		 */
		template<typename PointType>
		void ExtendTo(TArray<PointType>& Polyline, const PointType& DesiredEnd)
		{
			double DistanceStart = Polyline[0].SquareDistance(DesiredEnd);
			double DistanceEnd = Polyline.Last().SquareDistance(DesiredEnd);

			if (DistanceStart > DistanceEnd)
			{
				PointType Factor;
				for (int32 Index = 0; Index < PointType::Dimension(); ++Index)
				{
					Factor[Index] = FMath::Abs(Polyline.Last()[Index] - Polyline[0][Index]) > SMALL_NUMBER_SQUARE ? (DesiredEnd[Index] - Polyline[0][Index]) / (Polyline.Last()[Index] - Polyline[0][Index]) : 1.;
				}

				Algo::ForEach(Polyline, [&, Factor](PointType& Pole)
					{
						for (int32 Index = 0; Index < PointType::Dimension(); ++Index)
						{
							Pole[Index] = Polyline[0][Index] + (Pole[Index] - Polyline[0][Index]) * Factor[Index];
						}
					}
				);
			}
			else
			{
				PointType Factor;
				for (int32 Index = 0; Index < PointType::Dimension(); ++Index)
				{
					Factor[Index] = FMath::Abs(Polyline[0][Index] - Polyline.Last()[Index]) > SMALL_NUMBER_SQUARE ? (DesiredEnd[Index] - Polyline.Last()[Index]) / (Polyline[0][Index] - Polyline.Last()[Index]) : 1.;
				}

				Algo::Reverse(Polyline);
				Algo::ForEach(Polyline, [&, Factor](PointType& Pole)
					{
						for (int32 Index = 0; Index < PointType::Dimension(); ++Index)
						{
							Pole[Index] = Polyline.Last()[Index] + (Pole[Index] - Polyline.Last()[Index]) * Factor[Index];
						}
					}
				);
				Algo::Reverse(Polyline);
			}
		}
	}

	template<class PointType>
	class TPolylineApproximator
	{
	protected:
		const TArray<double>& PolylineCoordinates;
		const TArray<PointType>& PolylinePoints;

	public:
		TPolylineApproximator(const TArray<double>& InPolylineCoordinates, const TArray<PointType>& InPolylinePoints)
			: PolylineCoordinates(InPolylineCoordinates)
			, PolylinePoints(InPolylinePoints)
		{
		}

	protected:

		double ComputeCurvilinearCoordinatesOfPolyline(const FLinearBoundary& InBoundary, TArray<double>& OutCurvilinearCoordinates, int32& StartIndex, int32& EndIndex) const
		{
			GetStartEndIndex(InBoundary, StartIndex, EndIndex);

			OutCurvilinearCoordinates.Reserve(EndIndex - StartIndex + 2);

			double LastSegmentLength;
			double Length = 0;
			ensureCADKernel(EndIndex > StartIndex);

			OutCurvilinearCoordinates.Add(0);
			if (EndIndex > StartIndex + 1)
			{
				PointType StartPoint = ComputePoint(StartIndex, InBoundary.Min);
				PointType EndPoint = ComputePoint(EndIndex, InBoundary.Max);
				Length = StartPoint.Distance(PolylinePoints[StartIndex + 1]);
				LastSegmentLength = EndPoint.Distance(PolylinePoints[EndIndex]);

				OutCurvilinearCoordinates.Add(Length);
				for (int32 Index = StartIndex + 1; Index < EndIndex; ++Index)
				{
					Length += PolylinePoints[Index].Distance(PolylinePoints[Index + 1]);
					OutCurvilinearCoordinates.Add(Length);
				}
				Length += LastSegmentLength;
				OutCurvilinearCoordinates.Add(Length);
			}
			else
			{
				PointType StartPoint = ComputePoint(StartIndex, InBoundary.Min);
				PointType EndPoint = ComputePoint(EndIndex, InBoundary.Max);
				Length = StartPoint.Distance(EndPoint);
				OutCurvilinearCoordinates.Add(Length);
			}
			return Length;
		}

		PointType ComputePoint(const int32 Index, const double PointCoordinate) const
		{
			double Delta = PolylineCoordinates[Index + 1] - PolylineCoordinates[Index];
			if (FMath::IsNearlyZero(Delta, (double)KINDA_SMALL_NUMBER))
			{
				return PolylinePoints[Index];
			}

			return PolylinePoints[Index] + (PolylinePoints[Index + 1] - PolylinePoints[Index]) * (PointCoordinate - PolylineCoordinates[Index]) / Delta;
		};

		/**
		 * Project a Set of points on a restricted polyline (StartIndex & EndIndex define the polyline boundary)
		 * the points are projected on all segments of the polyline, the closest are selected
		 */
		double ProjectPointToPolyline(int32 StartIndex, int32 EndIndex, const PointType& InPointToProject, PointType& OutProjectedPoint) const
		{
			double MinDistance = HUGE_VAL;
			double UForMinDistance = 0;

			double ParamU = 0.;
			int32 SegmentIndex = 0;

			for (int32 Index = StartIndex; Index <= EndIndex; ++Index)
			{
				FPoint ProjectPoint = ProjectPointOnSegment(InPointToProject, PolylinePoints[Index], PolylinePoints[Index + 1], ParamU, true);
				double SquareDistance = FMath::Square(ProjectPoint[0] - InPointToProject[0]);
				if (SquareDistance > MinDistance)
				{
					continue;
				}
				SquareDistance += FMath::Square(ProjectPoint[1] - InPointToProject[1]);
				if (SquareDistance > MinDistance)
				{
					continue;
				}
				SquareDistance += FMath::Square(ProjectPoint[2] - InPointToProject[2]);
				if (SquareDistance > MinDistance)
				{
					continue;
				}
				MinDistance = SquareDistance;
				UForMinDistance = ParamU;
				SegmentIndex = Index;
				OutProjectedPoint = ProjectPoint;
			}

			return PolylineTools::LinearInterpolation(PolylineCoordinates, SegmentIndex, UForMinDistance);
		}

	public:

		void GetStartEndIndex(const FLinearBoundary& InBoundary, int32& StartIndex, int32& EndIndex) const
		{
			FDichotomyFinder Finder(PolylineCoordinates);
			StartIndex = Finder.Find(InBoundary.Min);
			EndIndex = Finder.Find(InBoundary.Max);
		}

		/**
		 * Evaluate the point of the polyline at the input InCoordinate
		 * If the input coordinate is outside the boundary of the polyline, the coordinate of the nearest boundary is used.
		 */
		PointType ApproximatePoint(const double InCoordinate) const
		{
			FDichotomyFinder Finder(PolylineCoordinates);
			int32 Index = Finder.Find(InCoordinate);
			return ComputePoint(Index, InCoordinate);
		}

		/**
		 * Evaluate the point of the polyline at the input Coordinate
		 * If the input coordinate is outside the boundary of the polyline, the coordinate of the nearest boundary is used.
		 */
		template<class CurvePointType>
		void ApproximatePoint(double InCoordinate, CurvePointType& OutPoint, int32 InDerivativeOrder) const
		{
			FDichotomyFinder Finder(PolylineCoordinates);
			int32 Index = Finder.Find(InCoordinate);

			OutPoint.DerivativeOrder = InDerivativeOrder;

			double DeltaU = PolylineCoordinates[Index + 1] - PolylineCoordinates[Index];
			if (FMath::IsNearlyZero(DeltaU))
			{
				OutPoint.Point = PolylinePoints[Index];
				OutPoint.Gradient = FPoint::ZeroPoint;
				OutPoint.Laplacian = FPoint::ZeroPoint;
				return;
			}

			double SectionCoordinate = (InCoordinate - PolylineCoordinates[Index]) / DeltaU;

			FPoint Tangent = PolylinePoints[Index + 1] - PolylinePoints[Index];
			OutPoint.Point = PolylinePoints[Index] + Tangent * SectionCoordinate;

			if (InDerivativeOrder > 0)
			{
				OutPoint.Gradient = Tangent;
				OutPoint.Laplacian = FPoint::ZeroPoint;
			}
		}

		template<class CurvePointType>
		void ApproximatePoints(const TArray<double>& InCoordinates, TArray<CurvePointType>& OutPoints, int32 InDerivativeOrder = 0) const
		{
			if (!InCoordinates.Num())
			{
				ensureCADKernel(false);
				return;
			}

			TFunction<void(FIndexOfCoordinateFinder&)> ComputePoints = [&](FIndexOfCoordinateFinder& Finder)
			{
				for (int32 IPoint = 0; IPoint < InCoordinates.Num(); ++IPoint)
				{
					int32 Index = Finder.Find(InCoordinates[IPoint]);

					OutPoints[IPoint].DerivativeOrder = InDerivativeOrder;

					double DeltaU = PolylineCoordinates[Index + 1] - PolylineCoordinates[Index];
					if (FMath::IsNearlyZero(DeltaU))
					{
						OutPoints[IPoint].Point = PolylinePoints[Index];
						OutPoints[IPoint].Gradient = FPoint::ZeroPoint;
						OutPoints[IPoint].Laplacian = FPoint::ZeroPoint;
						return;
					}

					double SectionCoordinate = (InCoordinates[IPoint] - PolylineCoordinates[Index]) / DeltaU;

					FPoint Tangent = PolylinePoints[Index + 1] - PolylinePoints[Index];
					OutPoints[IPoint].Point = PolylinePoints[Index] + Tangent * SectionCoordinate;

					if (InDerivativeOrder > 0)
					{
						OutPoints[IPoint].Gradient = Tangent;
						OutPoints[IPoint].Laplacian = FPoint::ZeroPoint;
					}
				}
			};

			FDichotomyFinder DichotomyFinder(PolylineCoordinates);

			int32 StartIndex = DichotomyFinder.Find(InCoordinates[0]);
			int32 EndIndex = DichotomyFinder.Find(InCoordinates.Last());
			bool bUseDichotomy = PolylineTools::IsDichotomyToBePreferred(EndIndex - StartIndex, InCoordinates.Num());

			OutPoints.Empty(InCoordinates.Num());
			if (bUseDichotomy)
			{
				DichotomyFinder.StartLower = StartIndex;
				DichotomyFinder.StartUpper = EndIndex;
				ComputePoints(DichotomyFinder);
			}
			else
			{
				FLinearFinder LinearFinder(PolylineCoordinates, StartIndex);
				ComputePoints(LinearFinder);
			}
		}


		/**
		 * Evaluate the points of the polyline associated to the increasing array of input Coordinates
		 * If the input coordinate is outside the boundary of the polyline, the coordinate of the nearest boundary is used.
		 */
		void ApproximatePoints(const TArray<double>& InCoordinates, TArray<PointType>& OutPoints) const
		{
			if (!InCoordinates.Num())
			{
				return;
			}

			TFunction<void(FIndexOfCoordinateFinder&)> ComputePoints = [&](FIndexOfCoordinateFinder& Finder)
			{
				for (double Coordinate : InCoordinates)
				{
					int32 Index = Finder.Find(Coordinate);
					OutPoints.Emplace(ComputePoint(Index, Coordinate));
				}
			};

			FDichotomyFinder DichotomyFinder(PolylineCoordinates);

			int32 StartIndex = DichotomyFinder.Find(InCoordinates[0]);
			int32 EndIndex = DichotomyFinder.Find(InCoordinates.Last());
			bool bUseDichotomy = PolylineTools::IsDichotomyToBePreferred(EndIndex - StartIndex, InCoordinates.Num());

			OutPoints.Empty(InCoordinates.Num());
			if (bUseDichotomy)
			{
				DichotomyFinder.StartLower = StartIndex;
				DichotomyFinder.StartUpper = EndIndex;
				ComputePoints(DichotomyFinder);
			}
			else
			{
				FLinearFinder LinearFinder(PolylineCoordinates, StartIndex);
				ComputePoints(LinearFinder);
			}
		}

		void SamplePolyline(const FLinearBoundary& InBoundary, const double DesiredSegmentLength, TArray<double>& OutCoordinates) const
		{
			int32 StartIndex = 0;
			int32 EndIndex = 0;

			TArray<double> CurvilinearCoordinates;
			double CurveLength = ComputeCurvilinearCoordinatesOfPolyline(InBoundary, CurvilinearCoordinates, StartIndex, EndIndex);

			int32 SegmentNum = (int32)FMath::Max(CurveLength / DesiredSegmentLength + 0.5, 1.0);

			double SectionLength = CurveLength / (double)(SegmentNum);

			OutCoordinates.Empty();
			OutCoordinates.Reserve(SegmentNum + 1);
			OutCoordinates.Add(InBoundary.Min);

			TFunction<double(const int32, const int32, const double, const double)> ComputeSamplePointCoordinate = [&](const int32 IndexCurvilinear, const int32 IndexCoordinate, const double Length, const double Coordinate)
			{
				return Coordinate + (PolylineCoordinates[IndexCoordinate] - Coordinate) * (Length - CurvilinearCoordinates[IndexCurvilinear - 1u]) / (CurvilinearCoordinates[IndexCurvilinear] - CurvilinearCoordinates[IndexCurvilinear - 1u]);
			};

			double CurvilinearLength = SectionLength;
			double LastCoordinate = InBoundary.Min;
			for (int32 IndexCurvilinear = 1, IndexCoordinate = StartIndex + 1; IndexCurvilinear < CurvilinearCoordinates.Num(); ++IndexCurvilinear, ++IndexCoordinate)
			{
				while (CurvilinearLength < CurvilinearCoordinates[IndexCurvilinear] + SMALL_NUMBER)
				{
					double Coordinate = ComputeSamplePointCoordinate(IndexCurvilinear, IndexCoordinate, CurvilinearLength, LastCoordinate);
					OutCoordinates.Add(Coordinate);
					CurvilinearLength += SectionLength;
					if (CurvilinearLength + SMALL_NUMBER > CurveLength)
					{
						OutCoordinates.Add(InBoundary.Max);
						break;
					}
				}
				LastCoordinate = PolylineCoordinates[IndexCoordinate];
			}
		}

		/**
		 * Project a Set of points on a restricted polyline (StartIndex & EndIndex define the polyline boundary)
		 * the points are projected on all segments of the polyline, the closest are selected
		 */
		double ProjectPointToPolyline(const FLinearBoundary& InBoundary, const PointType& PointOnEdge, PointType& OutProjectedPoint) const
		{
			int32 StartIndex = 0;
			int32 EndIndex = 0;
			GetStartEndIndex(InBoundary, StartIndex, EndIndex);

			return ProjectPointToPolyline(StartIndex, EndIndex, PointOnEdge, OutProjectedPoint);
		}

		/**
		 * Project a Set of points on a restricted polyline (StartIndex & EndIndex define the polyline boundary)
		 * Each points are projected on all segments of the restricted polyline, the closest are selected
		 */
		void ProjectPointsToPolyline(const FLinearBoundary& InBoundary, const TArray<PointType>& InPointsToProject, TArray<double>& OutProjectedPointCoords, TArray<PointType>& OutProjectedPoints) const
		{
			OutProjectedPointCoords.Empty(InPointsToProject.Num());
			OutProjectedPoints.Empty(InPointsToProject.Num());

			int32 StartIndex = 0;
			int32 EndIndex = 0;

			GetStartEndIndex(InBoundary, StartIndex, EndIndex);

			for (const PointType& Point : InPointsToProject)
			{
				PointType ProjectedPoint;
				double Coordinate = ProjectPointToPolyline(StartIndex, EndIndex, Point, ProjectedPoint);

				OutProjectedPointCoords.Emplace(Coordinate);
				OutProjectedPoints.Emplace(ProjectedPoint);
			}
		}

		/**
		 * Project each point of a coincidental polyline on the Polyline. 
		 */
		void ProjectCoincidentalPolyline(const TArray<PointType>& InPointsToProject, bool bSameOrientation, TArray<double>& OutProjectedPointCoords) const
		{
			int32 StartIndex = 0;
			const int32 EndIndex = PolylinePoints.Num() - 1;

			TFunction<double(const PointType&)> ProjectPointToPolyline = [&](const PointType & InPointToProject)
			{
				double MinDistance = HUGE_VAL;
				double UForMinDistance = 0;

				double ParamU = 0.;
				int32 SegmentIndex = 0;
				for (int32 Index = StartIndex; Index < EndIndex; ++Index)
				{
					PointType ProjectPoint = ProjectPointOnSegment(InPointToProject, PolylinePoints[Index], PolylinePoints[Index + 1], ParamU, true);

					double SquareDistance = ProjectPoint.SquareDistance(InPointToProject);
					if (SquareDistance > MinDistance)
					{
						break;
					}
					MinDistance = SquareDistance;
					UForMinDistance = ParamU;
					SegmentIndex = Index;
				}
				StartIndex = SegmentIndex;
				return PolylineTools::LinearInterpolation(PolylineCoordinates, SegmentIndex, UForMinDistance);
			};

			if (bSameOrientation)
			{
				OutProjectedPointCoords.Empty(InPointsToProject.Num());
				for (const PointType& Point : InPointsToProject)
				{
					double Coordinate = ProjectPointToPolyline(Point);
					OutProjectedPointCoords.Emplace(Coordinate);
				}
			}
			else
			{
				OutProjectedPointCoords.SetNum(InPointsToProject.Num());
				for (int32 Index = InPointsToProject.Num()-1, Pndex = 0; Index >= 0; --Index, ++Pndex)
				{
					OutProjectedPointCoords[Pndex] = ProjectPointToPolyline(InPointsToProject[Index]);
				}
			}
		}


		/**
		 * Append to the OutPoints array the sub polyline bounded by InBoundary according to the orientation
		 */
		void GetSubPolyline(const FLinearBoundary& InBoundary, const EOrientation Orientation, TArray<PointType>& OutPoints) const
		{
			int32 StartIndex = 0;
			int32 EndIndex = 0;
			GetStartEndIndex(InBoundary, StartIndex, EndIndex);

			int32 NewSize = OutPoints.Num() + EndIndex - StartIndex + 2;
			OutPoints.Reserve(NewSize);

			int32 PolylineStartIndex = StartIndex;
			int32 PolylineEndIndex = EndIndex;
			if (FMath::IsNearlyEqual(PolylineCoordinates[StartIndex + 1], InBoundary.Min, (double)KINDA_SMALL_NUMBER))
			{
				PolylineStartIndex++;
			}
			if (FMath::IsNearlyEqual(PolylineCoordinates[EndIndex], InBoundary.Max, (double)KINDA_SMALL_NUMBER))
			{
				PolylineEndIndex--;
			}

			if (Orientation)
			{
				OutPoints.Emplace(ComputePoint(StartIndex, InBoundary.Min));
				if(PolylineEndIndex - PolylineStartIndex > 0)
				{
					OutPoints.Append(PolylinePoints.GetData() + PolylineStartIndex + 1, PolylineEndIndex - PolylineStartIndex);
				}
				OutPoints.Emplace(ComputePoint(EndIndex, InBoundary.Max));
			}
			else
			{
				OutPoints.Emplace(ComputePoint(EndIndex, InBoundary.Max));
				for (int32 Index = PolylineEndIndex; Index > PolylineStartIndex; --Index)
				{
					OutPoints.Emplace(PolylinePoints[Index]);
				}
				OutPoints.Emplace(ComputePoint(StartIndex, InBoundary.Min));
			}
		}

		/**
		 * Get the subset of point defining the sub polyline bounded by InBoundary
		 * OutCoordinates and OutPoints are emptied before the process
		 */
		void GetSubPolyline(const FLinearBoundary& InBoundary, TArray<double>& OutCoordinates, TArray<PointType>& OutPoints) const
		{
			int32 StartIndex = 0;
			int32 EndIndex = 0;
			GetStartEndIndex(PolylineCoordinates, InBoundary, StartIndex, EndIndex);

			OutCoordinates.Empty(EndIndex - StartIndex + 2);
			OutPoints.Empty(EndIndex - StartIndex + 2);

			if (FMath::IsNearlyEqual(PolylineCoordinates[StartIndex + 1], InBoundary.Min, (double) KINDA_SMALL_NUMBER))
			{
				StartIndex++;
			}

			if (!FMath::IsNearlyEqual(PolylineCoordinates[EndIndex], InBoundary.Max, (double) KINDA_SMALL_NUMBER))
			{
				EndIndex--;
			}

			OutPoints.Emplace(ComputePoint(StartIndex, InBoundary.Min));
			OutPoints.Append(PolylinePoints.GetData() + StartIndex + 1, EndIndex - StartIndex);
			OutPoints.Emplace(ComputePoint(EndIndex, InBoundary.Max));

			OutCoordinates.Add(InBoundary.Min);
			OutCoordinates.Append(PolylineCoordinates.GetData() + StartIndex + 1, EndIndex - StartIndex);
			OutCoordinates.Add(InBoundary.Max);
		}

		double ComputeLength() const
		{
			double Length = 0;
			for (int32 Index = 1; Index < PolylinePoints.Num(); ++Index)
			{
				Length += PolylinePoints[Index-1].Distance(PolylinePoints[Index]);
			}
			return Length;
		}

		double ComputeLengthOfSubPolyline(const FLinearBoundary& InBoundary) const
		{
			int32 StartIndex = 0;
			int32 EndIndex = 0;

			GetStartEndIndex(InBoundary, StartIndex, EndIndex);

			double Length = 0;
			if (EndIndex > StartIndex)
			{
				FPoint StartPoint = ComputePoint(StartIndex, InBoundary.Min);
				FPoint EndPoint = ComputePoint(EndIndex, InBoundary.Max);
				Length = StartPoint.Distance(PolylinePoints[StartIndex + 1]);
				Length += EndPoint.Distance(PolylinePoints[EndIndex]);

				if (EndIndex > StartIndex + 1)
				{
					for (int32 Index = StartIndex + 1; Index < EndIndex; ++Index)
					{
						Length += PolylinePoints[Index].Distance(PolylinePoints[Index + 1]);
					}
				}
			}
			else
			{
				FPoint StartPoint = ComputePoint(StartIndex, InBoundary.Min);
				FPoint EndPoint = ComputePoint(EndIndex, InBoundary.Max);
				Length = StartPoint.Distance(EndPoint);
			}
			return Length;
		}


	};

} // namespace CADKernel

