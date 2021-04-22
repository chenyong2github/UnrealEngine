// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Geo/Surfaces/SphericalSurface.h"

#include "CADKernel/Geo/GeoPoint.h"
#include "CADKernel/Geo/Sampling/SurfacicSampling.h"


using namespace CADKernel;

void FSphericalSurface::EvaluatePoint(const FPoint2D& InSurfacicCoordinate, FSurfacicPoint& OutPoint3D, int32 InDerivativeOrder) const
{
	double CosU = cos(InSurfacicCoordinate.U);
	double CosV = cos(InSurfacicCoordinate.V);

	double SinU = sin(InSurfacicCoordinate.U);
	double SinV = sin(InSurfacicCoordinate.V);

	OutPoint3D.DerivativeOrder = InDerivativeOrder;
	OutPoint3D.Point.Set(Radius * CosV * CosU, Radius * CosV * SinU, Radius * SinV);
	OutPoint3D.Point = Matrix.Multiply(OutPoint3D.Point);

	if (InDerivativeOrder > 0)
	{
		OutPoint3D.GradientU = FPoint(-Radius * CosV * SinU, Radius * CosV * CosU, 0.0);
		OutPoint3D.GradientV = FPoint(-Radius * SinV * CosU, -Radius * SinV * SinU, Radius * CosV);

		OutPoint3D.GradientU = Matrix.MultiplyVector(OutPoint3D.GradientU);
		OutPoint3D.GradientV = Matrix.MultiplyVector(OutPoint3D.GradientV);
	}

	if (InDerivativeOrder > 1)
	{
		OutPoint3D.LaplacianU = FPoint(-Radius * CosV * CosU, -Radius * CosV * SinU, 0.0);
		OutPoint3D.LaplacianV = FPoint(OutPoint3D.LaplacianU.X, OutPoint3D.LaplacianU.Y, -Radius * SinV);
		OutPoint3D.LaplacianUV = FPoint(Radius * SinV * SinU, -Radius * SinV * CosU, 0.);

		OutPoint3D.LaplacianU = Matrix.MultiplyVector(OutPoint3D.LaplacianU);
		OutPoint3D.LaplacianV = Matrix.MultiplyVector(OutPoint3D.LaplacianV);
		OutPoint3D.LaplacianUV = Matrix.MultiplyVector(OutPoint3D.LaplacianUV);
	}
}

void FSphericalSurface::EvaluatePointGrid(const FCoordinateGrid& Coordinates, FSurfacicSampling& OutPoints, bool bComputeNormals) const
{
	OutPoints.bWithNormals = bComputeNormals;

	int32 PointNum = Coordinates.Count();
	OutPoints.Reserve(PointNum);

	OutPoints.Set2DCoordinates(Coordinates);

	int32 UCount = Coordinates.IsoCount(EIso::IsoU);

	TArray<double> CosU;
	TArray<double> SinU; 
	CosU.Reserve(UCount);
	SinU.Reserve(UCount);

	for(double Angle : Coordinates[EIso::IsoU])
	{
		CosU.Emplace(cos(Angle));
	}

	for (double Angle : Coordinates[EIso::IsoU])
	{
		SinU.Emplace(sin(Angle));
	}


	for (double VAngle : Coordinates[EIso::IsoV])
	{
		double CosV = cos(VAngle);
		double SinV = sin(VAngle);

		for (int32 Undex = 0; Undex < UCount; Undex++)
		{
			FPoint& Point = OutPoints.Points3D.Emplace_GetRef(Radius * CosV * CosU[Undex], Radius * CosV * SinU[Undex], Radius * SinV);
			Point = Matrix.Multiply(Point);
		}
	}

	if (bComputeNormals)
	{
		FPoint Center = Matrix.Column(3);
		for (FPoint& Point : OutPoints.Points3D)
		{
			OutPoints.Normals.Emplace(Point - Center);
		}

		OutPoints.NormalizeNormals();
	}
}

TSharedPtr<FEntityGeom> FSphericalSurface::ApplyMatrix(const FMatrixH& InMatrix) const
{
	FMatrixH NewMatrix = InMatrix * Matrix;
	return FEntity::MakeShared<FSphericalSurface>(Tolerance3D, NewMatrix, Radius, 
		Boundary[EIso::IsoU].Min, Boundary[EIso::IsoU].Max,
		Boundary[EIso::IsoV].Min, Boundary[EIso::IsoV].Max);
}

#ifdef CADKERNEL_DEV
FInfoEntity& FSphericalSurface::GetInfo(FInfoEntity& Info) const
{
	return FSurface::GetInfo(Info)
		.Add(TEXT("Matrix"), Matrix)
		.Add(TEXT("Radius"), Radius)
		.Add(TEXT("MeridianStartAngle"), Boundary[EIso::IsoU].Min)
		.Add(TEXT("MeridianEndAngle"),   Boundary[EIso::IsoU].Max)
		.Add(TEXT("ParallelStartAngle"), Boundary[EIso::IsoV].Min)
		.Add(TEXT("ParallelEndAngle"),   Boundary[EIso::IsoV].Max);
}
#endif

