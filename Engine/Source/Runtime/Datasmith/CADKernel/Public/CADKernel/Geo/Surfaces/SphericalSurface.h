// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Geo/Surfaces/Surface.h"

namespace CADKernel
{
	class CADKERNEL_API FSphericalSurface : public FSurface
	{
		friend FEntity;

	protected:
		FMatrixH Matrix;
		double Radius;

		/**
		 * The spherical surface is defined its radius. 
		 *
		 * It's defined as the rotation around Z axis of an semicircle defined in the plan XY centered at the origin. 
		 *
		 * The cone is placed at its final position and orientation by the Matrix
		 * 
		 * The bounds of the spherical surface are defined as follow:
		 * Bounds[EIso::IsoU].Min = MeridianStartAngle
		 * Bounds[EIso::IsoU].Max = MeridianEndAngle
		 * Bounds[EIso::IsoV].Min = ParallelStartAngle
		 * Bounds[EIso::IsoV].Max = ParallelEndAngle
		 */
		FSphericalSurface(const double InToleranceGeometric, const FMatrixH& InMatrix, double InRadius, double InParallelStartAngle = 0.0, double InParallelEndAngle = 2.0 * PI, double InMeridianStartAngle = -PI / 2.0, double InMeridianEndAngle = PI / 2.0)
			: FSurface(InToleranceGeometric, InParallelStartAngle, InParallelEndAngle, InMeridianStartAngle, InMeridianEndAngle)
			, Matrix(InMatrix)
			, Radius(InRadius)
		{
			SetMinToleranceIso();
		}

		FSphericalSurface(FCADKernelArchive& Archive)
			: FSurface()
		{
			Serialize(Archive);
		}

		virtual void SetMinToleranceIso() const override
		{
			double Tolerance2D = Tolerance3D / Radius;

			FPoint Origin = Matrix.Multiply(FPoint::ZeroPoint);

			FPoint Point2DU{ 1 , 0, 0 };
			FPoint Point2DV{ 0, 1, 0 };

			double ToleranceU = Tolerance2D / ComputeScaleAlongAxis(Point2DU, Matrix, Origin);
			double ToleranceV = Tolerance2D / ComputeScaleAlongAxis(Point2DV, Matrix, Origin);

			MinToleranceIso.Set(ToleranceU, ToleranceV);
		}
	
	public:

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FSurface::Serialize(Ar);
			Ar << Matrix;
			Ar << Radius;
		}

		ESurface GetSurfaceType() const
		{
			return ESurface::Sphere;
		}

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity&) const override;
#endif

		virtual TSharedPtr<FEntityGeom> ApplyMatrix(const FMatrixH& InMatrix) const override;

		virtual void EvaluatePoint(const FPoint2D& InSurfacicCoordinate, FSurfacicPoint& OutPoint3D, int32 InDerivativeOrder = 0) const override;
		virtual void EvaluatePointGrid(const FCoordinateGrid& Coordinates, FSurfacicSampling& OutPoints, bool bComputeNormals = false) const override;

		virtual void Presample(const FSurfacicBoundary& InBoundaries, FCoordinateGrid& OutCoordinates) override
		{
			PresampleIsoCircle(InBoundaries, OutCoordinates, EIso::IsoU);
			PresampleIsoCircle(InBoundaries, OutCoordinates, EIso::IsoV);
		}
	};
}
