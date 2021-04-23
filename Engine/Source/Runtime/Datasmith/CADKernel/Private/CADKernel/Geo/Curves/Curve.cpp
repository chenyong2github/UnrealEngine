// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Geo/Curves/Curve.h"

#include "CADKernel/Core/KernelParameters.h"
#include "CADKernel/Core/System.h"
#include "CADKernel/Geo/Curves/BoundedCurve.h"
#include "CADKernel/Geo/Sampling/Polyline.h"
#include "CADKernel/Geo/Sampling/PolylineTools.h"
#include "CADKernel/Geo/Sampling/SurfacicPolyline.h"
#include "CADKernel/Geo/Sampler/SamplerOnChord.h"
#include "CADKernel/Geo/Sampler/SamplerOnParam.h"
#include "CADKernel/UI/Display.h"

namespace CADKernel
{
	double FCurve::GetLength() const
	{
		if (!GlobalLength.IsValid())
		{
			GlobalLength = ComputeLength(Boundary);
		}
		return GlobalLength;
	}

	double FCurve::GetParametricTolerance() const
	{
		if (!Tolerance1D.IsValid())
		{
			double Tolerance2DMax = (GetUMax() - GetUMin()) * 0.01;
			double Length = GetLength();
			if (Length < SMALL_NUMBER)
			{
				Tolerance1D = Tolerance2DMax;
				return Tolerance1D;
			}

			Tolerance1D = Tolerance * (GetUMax() - GetUMin()) / Length;
			Tolerance1D = FMath::Max(Tolerance2DMax, (double)Tolerance1D);
		}
		return Tolerance1D;
	}

	void FCurve::EvaluatePoints(const TArray<double>& Coordinates, TArray<FCurvePoint>& OutPoints, int32 DerivativeOrder) const
	{
		OutPoints.SetNum(Coordinates.Num());
		for (int32 iPoint = 0; iPoint < Coordinates.Num(); iPoint++)
		{
			EvaluatePoint(Coordinates[iPoint], OutPoints[iPoint], DerivativeOrder);
		}
	}

	void FCurve::EvaluatePoints(const TArray<double>& Coordinates, TArray<FPoint>& OutPoints) const
	{
		OutPoints.Empty(Coordinates.Num());
		for (double Coordinate : Coordinates)
		{
			FCurvePoint Point;
			EvaluatePoint(Coordinate, Point, 0);
			OutPoints.Emplace(Point.Point);
		}
	}

	void FCurve::Evaluate2DPoints(const TArray<double>& Coordinates, TArray<FCurvePoint2D>& OutPoints, int32 DerivativeOrder) const
	{
		OutPoints.SetNum(Coordinates.Num());
		for (int32 iPoint = 0; iPoint < Coordinates.Num(); iPoint++)
		{
			Evaluate2DPoint(Coordinates[iPoint], OutPoints[iPoint], DerivativeOrder);
		}
	}

	/**
	 * Evaluate exact 2D points of the curve at the input Coordinates
	 * The function can only be used with 2D curve (Dimension == 2)
	 */
	void FCurve::Evaluate2DPoints(const TArray<double>& Coordinates, TArray<FPoint2D>& OutPoints) const
	{
		OutPoints.SetNum(Coordinates.Num());
		for (int32 iPoint = 0; iPoint < Coordinates.Num(); iPoint++)
		{
			Evaluate2DPoint(Coordinates[iPoint], OutPoints[iPoint]);
		}
	}

#ifdef CADKERNEL_DEV
	FInfoEntity& FCurve::GetInfo(FInfoEntity& Info) const
	{
		return FEntity::GetInfo(Info)
			.Add(TEXT("Curve type"), CurvesTypesNames[(uint8)GetCurveType()])
			.Add(TEXT("Dimension"), (int32) Dimension)
			.Add(TEXT("Boundary"), Boundary)
			.Add(TEXT("Length"), GetLength());
	}
#endif

	void FCurve::FindNotDerivableCoordinates(int32 DerivativeOrder, TArray<double>& OutNotDerivableCoordinates) const
	{
		FindNotDerivableCoordinates(Boundary, DerivativeOrder, OutNotDerivableCoordinates);
	}

	void FCurve::FindNotDerivableCoordinates(const FLinearBoundary& InBoundary, int32 DerivativeOrder, TArray<double>& OutNotDerivableCoordinates) const
	{
	}

	TSharedPtr<FCurve> FCurve::ReboundCurve(const FLinearBoundary& InBoundary)
	{
		FLinearBoundary NewBoundary = InBoundary;
		if (NewBoundary.Min < GetUMin())
		{
			NewBoundary.Min = GetUMin();
		}

		if (NewBoundary.Max > GetUMax())
		{
			NewBoundary.Max = GetUMax();
		}

		if (NewBoundary.IsDegenerated())
		{
			FMessage::Printf(Log, TEXT("Invalid bounds (u1=%f u2=%f) on curve %d\n"), NewBoundary.Min, NewBoundary.Max, GetId());
			return TSharedPtr<FCurve>();
		}

		if (FMath::IsNearlyEqual(NewBoundary.Min, GetUMin()) && FMath::IsNearlyEqual(NewBoundary.Max, GetUMax()))
		{
			FMessage::Printf(Debug, TEXT("Invalid rebound (UMin and UMax are nearly equal) on curve %d\n"), GetId());
			return TSharedPtr<FCurve>();
		}

		return FEntity::MakeShared<FBoundedCurve>(Tolerance, StaticCastSharedRef<FCurve>(AsShared()), NewBoundary, Dimension);
	}

	double FCurve::ComputeLength(const FLinearBoundary& InBoundary) const
	{
		FPolyline3D Polyline;
		FCurveSamplerOnChord Sampler(*this, Boundary, Tolerance, Polyline);
		Sampler.Sample();
		return Polyline.GetLength(Boundary);
	}

	void FCurve::Presample(const FLinearBoundary& InBoundary, TArray<double>& OutSampling) const
	{
		FPolyline3D Presampling;
		FCurveSamplerOnParam Sampler(*this, Boundary, Tolerance * 10., Tolerance, Presampling);
		Sampler.Sample();

		Presampling.SwapCoordinates(OutSampling);
	}

} // namespace CADKernel
