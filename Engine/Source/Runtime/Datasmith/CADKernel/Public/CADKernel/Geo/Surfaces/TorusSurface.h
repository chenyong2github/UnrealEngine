// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Geo/Surfaces/Surface.h"

namespace CADKernel
{
	class CADKERNEL_API FTorusSurface : public FSurface
	{
		friend FEntity;

	protected:
		FMatrixH Matrix;
		double MajorRadius;
		double MinorRadius;

		/**
		 * A torus is the solid formed by revolving a circular disc about a specified coplanar axis. 
		 * MajorRadius is the distance from the axis to the center of the defining disc, and MinorRadius is the radius of the defining disc,
		 * where MajorRadius > MinorRadius > 0.0. 
		 *
		 * The torus computed at the origin with Z axis.
		 * It is placed at its final position and orientation by the Matrix
		 *
		 * The bounds of the cone are defined as follow:
		 * Bounds[EIso::IsoU].Min = MajorStartAngle
		 * Bounds[EIso::IsoU].Max = MajorEndAngle
		 * Bounds[EIso::IsoV].Min = MinorStartAngle
		 * Bounds[EIso::IsoV].Max = MinorEndAngle
		 */
		FTorusSurface(const double InToleranceGeometric, const FMatrixH& InMatrix, double InMajorRadius, double InMinorRadius, double InMajorStartAngle = 0.0, double InMajorEndAngle = 2.0 * PI, double InMinorStartAngle = 0.0, double InMinorEndAngle = 2.0 * PI)
			: FSurface(InToleranceGeometric, InMajorStartAngle, InMajorEndAngle, InMinorStartAngle, InMinorEndAngle)
			, Matrix(InMatrix)
			, MajorRadius(InMajorRadius)
			, MinorRadius(InMinorRadius)
		{
			SetMinToleranceIso();
		}

		FTorusSurface(FCADKernelArchive& Archive)
			: FSurface()
		{
			Serialize(Archive);
		}

		void SetMinToleranceIso() const
		{
			double Tolerance2DU = Tolerance3D / MajorRadius;
			double Tolerance2DV = Tolerance3D / MinorRadius;

			FPoint Origin = Matrix.Multiply(FPoint::ZeroPoint);

			FPoint Point2DU{ 1 , 0, 0 };
			FPoint Point2DV{ 0, 1, 0 };

			Tolerance2DU /= ComputeScaleAlongAxis(Point2DU, Matrix, Origin);
			Tolerance2DV /= ComputeScaleAlongAxis(Point2DV, Matrix, Origin);

			MinToleranceIso.Set(Tolerance2DU, Tolerance2DV);
		}
		
	public:

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FSurface::Serialize(Ar);
			Ar << Matrix;
			Ar << MajorRadius;
			Ar << MinorRadius;
		}

		ESurface GetSurfaceType() const
		{
			return ESurface::Torus;
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

