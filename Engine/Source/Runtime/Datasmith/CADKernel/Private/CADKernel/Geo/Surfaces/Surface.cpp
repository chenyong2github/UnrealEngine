// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Geo/Surfaces/Surface.h"

#include "CADKernel/Core/KernelParameters.h"
#include "CADKernel/Core/System.h"

#include "CADKernel/Geo/GeoPoint.h"
#include "CADKernel/Geo/Sampler/SamplerOnParam.h"
#include "CADKernel/Geo/Sampling/SurfacicPolyline.h"
#include "CADKernel/Geo/Sampling/SurfacicSampling.h"

#include "CADKernel/Mesh/Structure/Grid.h"

namespace CADKernel
{
#ifdef CADKERNEL_DEV
	FInfoEntity& FSurface::GetInfo(FInfoEntity& Info) const
	{
		return FEntityGeom::GetInfo(Info)
			.Add(TEXT("Surface type"), SurfacesTypesNames[(uint8)GetSurfaceType()])
			.Add(TEXT("Boundary"), Boundary);
	}
#endif

	void FSurface::Sample(const FSurfacicBoundary& InBoundary, int32 NumberOfSubdivisions[2], FCoordinateGrid& OutCoordinateSampling) const
	{
		for (int32 Index = EIso::IsoU; Index <= EIso::IsoV; Index++)
		{
			EIso Iso = (EIso)Index;
			double Step = (InBoundary[Iso].Max - InBoundary[Iso].Min) / ((double)(NumberOfSubdivisions[Iso] - 1));

			OutCoordinateSampling[Iso].Empty(NumberOfSubdivisions[Iso]);
			double Value = InBoundary[Iso].Min;
			for (int32 Kndex = 0; Kndex < NumberOfSubdivisions[Iso]; Kndex++)
			{
				OutCoordinateSampling[Iso].Add(Value);
				Value += Step;
			}
		}
	}

	void FSurface::Sample(const FSurfacicBoundary& Bounds, int32 NumberOfSubdivisions[2], FSurfacicSampling& OutPointSampling) const
	{
		FCoordinateGrid CoordinateSampling;
		Sample(Bounds, NumberOfSubdivisions, CoordinateSampling);
		EvaluatePointGrid(CoordinateSampling, OutPointSampling);
	}

	void FSurface::Presample(const FSurfacicBoundary& InBoundaries, FCoordinateGrid& OutCoordinates)
	{
		FSurfaceSamplerOnParam Sampler(*this, InBoundaries, Tolerance3D * 10, Tolerance3D, OutCoordinates);
		Sampler.Sample();
	}

	const void FSurface::ComputeIsoTolerances() const
	{
		FSurfacicSampling Grid;
		int32 NumberOfSubdivisions[2] = { 10, 10 };

		Sample(Boundary, NumberOfSubdivisions, Grid);

		const FSurfacicBoundary& SurfaceBoundaries = GetBoundary();

		FSurfacicTolerance& Tolerances = ToleranceIsos;

		TFunction<double(double, double)> ComputeTolerance = [&](double CurveLength3D, double CurveLength2D)
		{
			if (CurveLength3D > SMALL_NUMBER)
			{
				double Tolerance2D = Tolerance3D * CurveLength2D / CurveLength3D;
				return FMath::Max(FMath::Min(Tolerance2D, CurveLength2D / 2.), SMALL_NUMBER_SQUARE);
			}
			return CurveLength2D / 2.;
		};

		// Compute 2D tolerance along U
		{
			double MaxLengthIsoV = 0.0;

			// For each isoV curve
			for (int32 Index1 = 0, IndexPoint = 0; Index1 < NumberOfSubdivisions[EIso::IsoV]; ++Index1, ++IndexPoint)
			{
				double Length = 0.0;

				// Compute the length of the curve
				for (int32 Index2 = 0; Index2 < NumberOfSubdivisions[EIso::IsoU] - 1; ++Index2, IndexPoint ++)
				{
					Length += Grid.Points3D[IndexPoint].Distance(Grid.Points3D[IndexPoint + 1]);
				}

				if (Length > MaxLengthIsoV)
				{
					MaxLengthIsoV = Length;
				}
			}

			Tolerances[EIso::IsoU] = ComputeTolerance(MaxLengthIsoV, SurfaceBoundaries[EIso::IsoU].Length());
		};

		// Compute 2D tolerance along V
		{
			double MaxLengthIsoU = 0.0;

			int32 IndexPoint = 0;
			int32 Delta = NumberOfSubdivisions[EIso::IsoU];

			// For each isoU curve
			for (int32 Index1 = 0; Index1 < NumberOfSubdivisions[EIso::IsoU]; ++Index1, ++IndexPoint)
			{
				double Length = 0.0;
					IndexPoint = Index1;

				// Compute the length of the curve
				for (int32 Index2 = 0; Index2 < NumberOfSubdivisions[EIso::IsoV] - 1; ++Index2, IndexPoint += Delta)
				{
					Length += Grid.Points3D[IndexPoint].Distance(Grid.Points3D[IndexPoint + Delta]);
				}

				if (Length > MaxLengthIsoU)
				{
					MaxLengthIsoU = Length;
				}
			}

			Tolerances[EIso::IsoV] = ComputeTolerance(MaxLengthIsoU, SurfaceBoundaries[EIso::IsoV].Length());
		};
	}

	void FSurface::EvaluatePoints(const TArray<FPoint2D>& InSurfacicCoordinates, TArray<FSurfacicPoint>& OutPoint3D, int32 InDerivativeOrder) const
	{
		OutPoint3D.SetNum(InSurfacicCoordinates.Num());
		for (int32 Index = 0; Index < InSurfacicCoordinates.Num(); ++Index)
		{
			EvaluatePoint(InSurfacicCoordinates[Index], OutPoint3D[Index], InDerivativeOrder);
		}
	}

	void FSurface::EvaluatePoints(const TArray<FCurvePoint2D>& InSurfacicCoordinates, TArray<FSurfacicPoint>& OutPoint3D, int32 InDerivativeOrder) const
	{
		OutPoint3D.SetNum(InSurfacicCoordinates.Num());
		for (int32 Index = 0; Index < InSurfacicCoordinates.Num(); Index++)
		{
			EvaluatePoint(InSurfacicCoordinates[Index].Point, OutPoint3D[Index], InDerivativeOrder);
		}
	}

	void FSurface::EvaluatePoints(const TArray<FCurvePoint2D>& InSurfacicCoordinates, TArray<FCurvePoint>& OutPoints3D, int32 InDerivativeOrder) const
	{
		OutPoints3D.SetNum(InSurfacicCoordinates.Num());
		TArray<FSurfacicPoint> SurfacicPoints3D;
		EvaluatePoints(InSurfacicCoordinates, SurfacicPoints3D, InDerivativeOrder);
		for(int32 Index = 0; Index < InSurfacicCoordinates.Num(); ++Index)
		{
			OutPoints3D[Index].Combine(InSurfacicCoordinates[Index], SurfacicPoints3D[Index]);
		}
	}

	void FSurface::EvaluatePoints(FSurfacicPolyline& Polyline) const
	{
		int32 DerivativeOrder = Polyline.bWithNormals ? 1 : 0;

		TArray<FSurfacicPoint> Point3D;
		EvaluatePoints(Polyline.Points2D, Point3D, DerivativeOrder);

		Polyline.Points3D.Empty(Polyline.Points2D.Num());
		for (FSurfacicPoint Point : Point3D)
		{
			Polyline.Points3D.Emplace(Point.Point);
		}

		if (Polyline.bWithNormals)
		{
			Polyline.Normals.Empty(Polyline.Points2D.Num());
			for (FSurfacicPoint Point : Point3D)
			{
				Polyline.Normals.Emplace(Point.GradientU ^ Point.GradientV);
			}
			for (FPoint Normal : Polyline.Normals)
			{
				Normal.Normalize();
			}
		}
	}

	void FSurface::EvaluatePoints(const TArray<FCurvePoint2D>& InPoints2D, FSurfacicPolyline& Polyline) const
	{
		int32 DerivativeOrder = 1;

		TArray<FSurfacicPoint> Point3D;
		EvaluatePoints(InPoints2D, Point3D, DerivativeOrder);

		Polyline.Points2D.Empty(InPoints2D.Num());
		Polyline.Points3D.Empty(InPoints2D.Num());
		Polyline.Tangents.Empty(InPoints2D.Num());
		Polyline.Normals.Empty(InPoints2D.Num());

		for (const FCurvePoint2D& Point : InPoints2D)
		{
			Polyline.Points2D.Emplace(Point.Point);
		}

		for (const FSurfacicPoint& Point : Point3D)
		{
			Polyline.Points3D.Emplace(Point.Point);
		}

		for (int32 Index = 0; Index < InPoints2D.Num(); ++Index)
		{
			Polyline.Tangents.Emplace(Point3D[Index].GradientU * InPoints2D[Index].Gradient.U + Point3D[Index].GradientV * InPoints2D[Index].Gradient.V);
		}

		for (const FSurfacicPoint& Point : Point3D)
		{
			Polyline.Normals.Emplace(Point.GradientU ^ Point.GradientV);
		}

		for (FPoint& Normal : Polyline.Normals)
		{
			Normal.Normalize();
		}
	}

	void FSurface::EvaluateNormals(const TArray<FPoint2D>& InPoints2D, TArray<FPoint>& Normals) const
	{
		int32 DerivativeOrder = 1;
		TArray<FSurfacicPoint> Point3D;
		EvaluatePoints(InPoints2D, Point3D, DerivativeOrder);

		Normals.Empty(InPoints2D.Num());

		for (const FSurfacicPoint& Point : Point3D)
		{
			Normals.Emplace(Point.GradientU ^ Point.GradientV);
		}

		for (FPoint& Normal : Normals)
		{
			Normal.Normalize();
		}
	}

	void FSurface::EvaluatePointGrid(const FCoordinateGrid& Coordinates, FSurfacicSampling& OutPoints, bool bComputeNormals) const
	{
		OutPoints.Reserve(Coordinates.Count());

		OutPoints.Set2DCoordinates(Coordinates);

		int32 DerivativeOrder = bComputeNormals ? 1 : 0;
		TArray<FSurfacicPoint> Point3D;
		EvaluatePoints(OutPoints.Points2D, Point3D, DerivativeOrder);

		OutPoints.bWithNormals = bComputeNormals;

		for (FSurfacicPoint Point : Point3D)
		{
			OutPoints.Points3D.Emplace(Point.Point);
		}

		if (bComputeNormals)
		{
			for (FSurfacicPoint Point : Point3D)
			{
				OutPoints.Normals.Emplace(Point.GradientU ^ Point.GradientV);
			}
			OutPoints.NormalizeNormals();
		}
	}

	void FSurface::EvaluateGrid(FGrid& Grid) const
	{
		FSurfacicSampling OutPoints;

		const FCoordinateGrid& CoordinateGrid = Grid.GetCuttingCoordinates();
		EvaluatePointGrid(CoordinateGrid, OutPoints, true);

		Swap(OutPoints.Points3D, Grid.GetInner3DPoints());
		Swap(OutPoints.Normals, Grid.GetNormals());
	}

} // namespace CADKernel

