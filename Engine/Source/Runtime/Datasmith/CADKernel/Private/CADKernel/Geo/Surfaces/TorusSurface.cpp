// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Geo/Surfaces/TorusSurface.h"

#include "CADKernel/Geo/GeoPoint.h"
#include "CADKernel/Geo/Sampling/SurfacicSampling.h"

using namespace CADKernel;

TSharedPtr<FEntityGeom> FTorusSurface::ApplyMatrix(const FMatrixH& InMatrix) const
{
	FMatrixH NewMatrix = InMatrix * Matrix;
	return FEntity::MakeShared<FTorusSurface>(Tolerance3D, NewMatrix, MajorRadius, MinorRadius, Boundary[EIso::IsoU].Min, Boundary[EIso::IsoU].Max, Boundary[EIso::IsoV].Min, Boundary[EIso::IsoV].Max);
}

#ifdef CADKERNEL_DEV
FInfoEntity& FTorusSurface::GetInfo(FInfoEntity& Info) const
{
	return FSurface::GetInfo(Info)
		.Add(TEXT("Matrix"), Matrix)
		.Add(TEXT("MajorRadius"), MajorRadius)
		.Add(TEXT("MinorRadius"), MinorRadius)
		.Add(TEXT("MajorStartAngle"), Boundary[EIso::IsoU].Min)
		.Add(TEXT("MajorEndAngle"), Boundary[EIso::IsoU].Max)
		.Add(TEXT("MinorStartAngle"), Boundary[EIso::IsoV].Min)
		.Add(TEXT("MinorEndAngle"), Boundary[EIso::IsoV].Max);
}
#endif

void FTorusSurface::EvaluatePoint(const FPoint2D& InSurfacicCoordinate, FSurfacicPoint& OutPoint3D, int32 InDerivativeOrder) const
{
	double CosU = cos(InSurfacicCoordinate.U);
	double CosV = cos(InSurfacicCoordinate.V);

	double SinU = sin(InSurfacicCoordinate.U);
	double SinV = sin(InSurfacicCoordinate.V);

	OutPoint3D.DerivativeOrder = InDerivativeOrder;
	OutPoint3D.Point.Set((MajorRadius + MinorRadius * CosV) * CosU, (MajorRadius + MinorRadius * CosV) * SinU, MinorRadius * SinV);

	OutPoint3D.Point = Matrix.Multiply(OutPoint3D.Point);

	if (InDerivativeOrder > 0) 
	{
		OutPoint3D.GradientU = FPoint((MajorRadius + MinorRadius * CosV) * -SinU, (MajorRadius + MinorRadius * CosV) * CosU, 0.0);
		OutPoint3D.GradientV = FPoint((MinorRadius * -SinV) * CosU, (MinorRadius * -SinV) * SinU, MinorRadius * CosV);

		OutPoint3D.GradientU = Matrix.MultiplyVector(OutPoint3D.GradientU);
		OutPoint3D.GradientV = Matrix.MultiplyVector(OutPoint3D.GradientV);

		if (InDerivativeOrder > 1)
		{
			OutPoint3D.LaplacianU = FPoint((MajorRadius + MinorRadius * CosV) * -CosU, (MajorRadius + MinorRadius * CosV) * -SinU, 0.0);
			OutPoint3D.LaplacianV = FPoint((MinorRadius * -CosV) * CosU, (MinorRadius * -CosV) * SinU, MinorRadius * -SinV);
			OutPoint3D.LaplacianUV = FPoint((MinorRadius * -SinV) * -SinU, (MinorRadius * -SinV) * CosU, 0.0);

			OutPoint3D.LaplacianU = Matrix.MultiplyVector(OutPoint3D.LaplacianU);
			OutPoint3D.LaplacianV = Matrix.MultiplyVector(OutPoint3D.LaplacianV);
			OutPoint3D.LaplacianUV = Matrix.MultiplyVector(OutPoint3D.LaplacianUV);
		}
	}
}

void FTorusSurface::EvaluatePointGrid(const FCoordinateGrid& Coordinates, FSurfacicSampling& OutPoints, bool bComputeNormals) const
{
	int32 PointNum = Coordinates.Count();

	OutPoints.bWithNormals = bComputeNormals;

	OutPoints.Reserve(PointNum);
	OutPoints.Set2DCoordinates(Coordinates);

	int32 UCount = Coordinates.IsoCount(EIso::IsoU);

	TArray<double> CosU;
	TArray<double> SinU;
	CosU.Reserve(UCount);
	SinU.Reserve(UCount);

	for (double Angle : Coordinates[EIso::IsoU])
	{
		CosU.Emplace(cos(Angle));
	}

	for (double Angle : Coordinates[EIso::IsoU])
	{
		SinU.Emplace(sin(Angle));
	}


	for (double Angle : Coordinates[EIso::IsoV])
	{
		double CosV = cos(Angle);
		double SinV = sin(Angle);

		for (int32 Undex = 0; Undex < UCount; Undex++)
		{
			OutPoints.Points3D.Emplace((MajorRadius + MinorRadius * CosV) * CosU[Undex], (MajorRadius + MinorRadius * CosV) * SinU[Undex], MinorRadius * SinV);
		}

		if (bComputeNormals)
		{
			for (int32 Undex = 0; Undex < UCount; Undex++)
			{
				FPoint GradientU = FPoint((MajorRadius + MinorRadius * CosV) * -SinU[Undex], (MajorRadius + MinorRadius * CosV) * CosU[Undex], 0.0);
				FPoint GradientV = FPoint((MinorRadius * -SinV) * CosU[Undex], (MinorRadius * -SinV) * SinU[Undex], MinorRadius * CosV);
				OutPoints.Normals.Emplace(GradientU ^ GradientV);
			}
		}
	}

	for (FPoint& Point : OutPoints.Points3D)
	{
		Point = Matrix.Multiply(Point);
	}

	if (bComputeNormals)
	{
		FVector Center = Matrix.Column(3);
		for (FVector& Normal : OutPoints.Normals)
		{
			 Normal = Matrix.PointRotation(Normal, Center);
		}
		OutPoints.NormalizeNormals();
	}
}
