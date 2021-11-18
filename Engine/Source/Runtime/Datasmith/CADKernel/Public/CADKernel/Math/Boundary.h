// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"
#include "CADKernel/Geo/GeoEnum.h"
#include "CADKernel/Math/MathConst.h"
#include "CADKernel/Math/Point.h"

namespace CADKernel
{
	/**
	 * MINIMAL_UNIT_LINEAR_TOLERANCE allows to define the minimal tolerance value of a parametric space
	 * @see FLinearBoundary::ComputeMinimalTolerance
	 */
	#define MINIMAL_UNIT_LINEAR_TOLERANCE 10e-5

	struct CADKERNEL_API FLinearBoundary
	{

		/** A default boundary (0., 1.)*/
		static const FLinearBoundary DefaultBoundary;

		double Min;
		double Max;

		FLinearBoundary()
		{
			Min = 0.;
			Max = 1.;
		}

		FLinearBoundary(const FLinearBoundary& Boundary)
			: Min(Boundary.Min)
			, Max(Boundary.Max)
		{
		}

		FLinearBoundary(double UMin, double UMax)
		{
			Set(UMin, UMax);
		}

		friend FArchive& operator<<(FArchive& Ar, FLinearBoundary& Boundary)
		{
			Ar.Serialize(&Boundary, sizeof(FLinearBoundary));
			return Ar;
		}

		constexpr double GetMin() const
		{
			return Min;
		}

		constexpr double GetMax() const
		{
			return Max;
		}

		constexpr double GetAt(double Coordinate) const
		{
			return Min + (Max - Min) * Coordinate;
		}

		constexpr double GetMiddle() const
		{
			return (Min + Max) * 0.5;
		}

		double Size() const { return Max - Min; }

		void SetMin(double Coordinates)
		{
			GetMinMax(Coordinates, Max, Min, Max);
		}

		void SetMax(double Coordinates)
		{
			GetMinMax(Min, Coordinates, Min, Max);
		}

		void Set(double InUMin = 0., double InUMax = 1.)
		{
			GetMinMax(InUMin, InUMax, Min, Max);
		}

		/**
		 * Set the boundary with the min and max of the array
		 */
		void Set(const TArray<double>& Coordinates)
		{
			Init();
			for (const double& Coordinate : Coordinates)
			{
				ExtendTo(Coordinate);
			}
		}


		bool IsValid() const
		{
			return Min <= Max;
		}

		bool Contains(double Coordinate) const
		{
			return RealCompare(Coordinate, Min) >= 0 && RealCompare(Coordinate, Max) <= 0;
		}

		double Length() const
		{
			return GetMax() - GetMin();
		}

		/**
		 * Return true if the parametric domain is to small
		 */
		bool IsDegenerated() const
		{
			double DeltaU = (Max - Min);
			return (DeltaU < KINDA_SMALL_NUMBER);
		}

		/**
		 * Compute the minimal tolerance of the parametric domain i.e. 
		 * ToleranceMin = Boundary.Length() * MINIMAL_UNIT_LINEAR_TOLERANCE
		 * e.g. for a curve of 1m with a parametric space define between [0, 1], the parametric tolerance is 0.01
		 * This is a minimal value that has to be replace with a more accurate value when its possible
		 */
		double ComputeMinimalTolerance() const
		{
			return Length() * MINIMAL_UNIT_LINEAR_TOLERANCE;
		}

		/**
		 * If a coordinate is outside the bounds, set the coordinate at the closed limit
		 */
		void MoveInsideIfNot(double& Coordinate, double Tolerance = SMALL_NUMBER) const
		{
			if (Coordinate <= Min)
			{
				Coordinate = Min + Tolerance;
			}
			else if (Coordinate >= Max)
			{
				Coordinate = Max - Tolerance;
			}
		}

		/**
		 * Uses to initiate a boundary computation with ExtendTo 
		 */
		void Init()
		{
			Min = HUGE_VALUE;
			Max = -HUGE_VALUE;
		}

		void ExtendTo(double MinCoordinate, double MaxCoordinate)
		{
			GetMinMax(MinCoordinate, MaxCoordinate);
			Min = FMath::Min(Min, MinCoordinate);
			Max = FMath::Max(Max, MaxCoordinate);
		}

		void TrimAt(const FLinearBoundary& MaxBound)
		{
			Min = FMath::Max(Min, MaxBound.Min);
			Max = FMath::Min(Max, MaxBound.Max);
		}

		void ExtendTo(const FLinearBoundary& MaxBound)
		{
			Min = FMath::Min(Min, MaxBound.Min);
			Max = FMath::Max(Max, MaxBound.Max);
		}

		void ExtendTo(double Coordinate)
		{
			if (Coordinate < Min)
			{
				Min = Coordinate;
			}

			if (Coordinate > Max)
			{
				Max = Coordinate;
			}
		}

		void RestrictTo(const FLinearBoundary& MaxBound)
		{
			if (MaxBound.Min > Min)
			{
				Min = MaxBound.Min;
			}
			if (MaxBound.Max < Max)
			{
				Max = MaxBound.Max;
			}
		}

		/**
		 * If the boundary width is near or equal to zero, it's widened by +/- SMALL_NUMBER
		 */
		void WidenIfDegenerated()
		{
			if (FMath::IsNearlyEqual(Min, Max))
			{
				Min -= SMALL_NUMBER;
				Max += SMALL_NUMBER;
			}
		}

		FLinearBoundary& operator=(const FLinearBoundary& InBounds)
		{
			Min = InBounds.Min;
			Max = InBounds.Max;
			return *this;
		}

	};

	class CADKERNEL_API FSurfacicBoundary
	{
	private:
		FLinearBoundary UVBoundaries[2];

	public:
		/** A default boundary (0., 1., 0., 1.)*/
		static const FSurfacicBoundary DefaultBoundary;

		FSurfacicBoundary() = default;

		FSurfacicBoundary(double InUMin, double InUMax, double InVMin, double InVMax)
		{
			UVBoundaries[EIso::IsoU].Set(InUMin, InUMax);
			UVBoundaries[EIso::IsoV].Set(InVMin, InVMax);
		}

		FSurfacicBoundary(const FPoint2D& Point1, const FPoint2D& Point2)
		{
			Set(Point1, Point2);
		}
		
		void Set(const FPoint2D& Point1, const FPoint2D& Point2)
		{
			UVBoundaries[EIso::IsoU].Set(Point1.U, Point2.U);
			UVBoundaries[EIso::IsoV].Set(Point1.V, Point2.V);
		}

		friend FArchive& operator<<(FArchive& Ar, FSurfacicBoundary& Boundary)
		{
			Ar << Boundary[EIso::IsoU];
			Ar << Boundary[EIso::IsoV];
			return Ar;
		}

		void Set(const FLinearBoundary& BoundU, const FLinearBoundary& BoundV)
		{
			UVBoundaries[EIso::IsoU] = BoundU;
			UVBoundaries[EIso::IsoV] = BoundV;
		}

		void Set(double InUMin, double InUMax, double InVMin, double InVMax)
		{
			UVBoundaries[EIso::IsoU].Set(InUMin, InUMax);
			UVBoundaries[EIso::IsoV].Set(InVMin, InVMax);
		}

		void Set()
		{
			UVBoundaries[EIso::IsoU].Set();
			UVBoundaries[EIso::IsoV].Set();
		}

		/**
		 * Set the boundary with the min and max of this array
		 */
		void Set(const TArray<FPoint2D>& Points)
		{
			Init();
			for (const FPoint2D& Point : Points)
			{
				ExtendTo(Point);
			}
		}

		const FLinearBoundary& Get(EIso Type) const
		{
			return UVBoundaries[Type];
		}

		bool IsValid() const
		{
			return UVBoundaries[EIso::IsoU].IsValid() && UVBoundaries[EIso::IsoV].IsValid();
		}

		/**
		 * Return true if the parametric domain is to small
		 */
		bool IsDegenerated() const
		{
			return UVBoundaries[EIso::IsoU].IsDegenerated() || UVBoundaries[EIso::IsoV].IsDegenerated();
		}

		/**
		 * Uses to initiate a boundary computation with ExtendTo
		 */
		void Init()
		{
			UVBoundaries[EIso::IsoU].Init();
			UVBoundaries[EIso::IsoV].Init();
		}

		void TrimAt(const FSurfacicBoundary MaxLimit)
		{
			UVBoundaries[EIso::IsoU].TrimAt(MaxLimit[EIso::IsoU]);
			UVBoundaries[EIso::IsoV].TrimAt(MaxLimit[EIso::IsoV]);
		}

		void ExtendTo(const FSurfacicBoundary MaxLimit)
		{
			UVBoundaries[EIso::IsoU].ExtendTo(MaxLimit[EIso::IsoU]);
			UVBoundaries[EIso::IsoV].ExtendTo(MaxLimit[EIso::IsoV]);
		}

		void ExtendTo(FPoint2D Point)
		{
			UVBoundaries[EIso::IsoU].ExtendTo(Point.U);
			UVBoundaries[EIso::IsoV].ExtendTo(Point.V);
		}

		void ExtendTo(FPoint Point)
		{
			UVBoundaries[EIso::IsoU].ExtendTo(Point.X);
			UVBoundaries[EIso::IsoV].ExtendTo(Point.Y);
		}

		void RestrictTo(const FSurfacicBoundary& MaxBound)
		{
			UVBoundaries[EIso::IsoU].RestrictTo(MaxBound.UVBoundaries[EIso::IsoU]);
			UVBoundaries[EIso::IsoV].RestrictTo(MaxBound.UVBoundaries[EIso::IsoV]);
		}

		/**
		 * If Along each axis, the bound width is near equal to zero, it's widened by +/- SMALL_NUMBER
		 */
		void WidenIfDegenerated()
		{
			UVBoundaries[EIso::IsoU].WidenIfDegenerated();
			UVBoundaries[EIso::IsoV].WidenIfDegenerated();
		}

		/**
		 * If a point is outside the bounds, set the coordinate to insert the point inside the bounds
		 */
		void MoveInsideIfNot(FPoint& Point, double Tolerance = SMALL_NUMBER) const
		{
			UVBoundaries[EIso::IsoU].MoveInsideIfNot(Point.X, Tolerance);
			UVBoundaries[EIso::IsoV].MoveInsideIfNot(Point.Y, Tolerance);
		}

		/**
		 * If a point is outside the bounds, set the coordinate to insert the point inside the bounds
		 */
		void MoveInsideIfNot(FPoint2D& Point, double Tolerance = SMALL_NUMBER) const
		{
			UVBoundaries[EIso::IsoU].MoveInsideIfNot(Point.U, Tolerance);
			UVBoundaries[EIso::IsoV].MoveInsideIfNot(Point.V, Tolerance);
		}

		FSurfacicBoundary& operator=(const FSurfacicBoundary& InBounds)
		{
			UVBoundaries[EIso::IsoU] = InBounds.UVBoundaries[EIso::IsoU];
			UVBoundaries[EIso::IsoV] = InBounds.UVBoundaries[EIso::IsoV];
			return *this;
		}

		double Length(const EIso& Iso) const
		{
			return UVBoundaries[Iso].Length();
		}

		constexpr const FLinearBoundary& operator[](const EIso& Iso) const
		{
			return UVBoundaries[Iso];
		}

		constexpr FLinearBoundary& operator[](const EIso& Iso)
		{
			return UVBoundaries[Iso];
		}
	};
}

