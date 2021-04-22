// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Geo/Surfaces/Surface.h"
#include "CADKernel/Math/Plane.h"

namespace CADKernel
{
	class CADKERNEL_API FPlaneSurface : public FSurface
	{
		friend FEntity;

	protected:

		FMatrixH Matrix;
		FMatrixH InverseMatrix;

		/**
		 * The plane surface is the plane XY.
		 * The surface is placed at its final position and orientation by the Matrix
		 */
		FPlaneSurface(const double InToleranceGeometric, const FMatrixH& InMatrix);

		/**
		 * The plane surface is the plane XY.
		 * The surface is placed at its final position and orientation by the Matrix,
		 * The matrix is calculated from the normal plane at its final position and its distance from the origin along the normal.
		 */
		FPlaneSurface(const double InToleranceGeometric, double DistanceFromOrigin, FPoint Normal)
			: FPlaneSurface(InToleranceGeometric, Normal* DistanceFromOrigin, Normal)
		{
		}

		/**
		 * The plane surface is the plane XY.
		 * The surface is placed at its final position and orientation by the Matrix 
		 * The matrix is calculated from the plan origine at its final position and its final normal
		 */
		FPlaneSurface(const double InToleranceGeometric, const FPoint& Position, FPoint Normal);

		FPlaneSurface(FCADKernelArchive& Archive)
			: FSurface()
		{
			Serialize(Archive);
		}
		

	public:

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FSurface::Serialize(Ar);
			Ar << Matrix;
			Ar << InverseMatrix;
		}

		ESurface GetSurfaceType() const
		{
			return ESurface::Plane;
		}

		const FMatrixH& GetMatrix() const
		{
			return Matrix;
		}

		FPlane GetPlane() const;

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity&) const override;
#endif

		virtual TSharedPtr<FEntityGeom> ApplyMatrix(const FMatrixH& InMatrix) const override;

		virtual void EvaluatePoint(const FPoint2D& InSurfacicCoordinate, FSurfacicPoint& OutPoint3D, int32 InDerivativeOrder = 0) const override;
		virtual void EvaluatePoints(const TArray<FPoint2D>& InSurfacicCoordinates, TArray<FSurfacicPoint>& OutPoint3D, int32 InDerivativeOrder = 0) const override;
		virtual void EvaluatePointGrid(const FCoordinateGrid& Coordinates, FSurfacicSampling& OutPoints, bool bComputeNormals = false) const override;

		virtual FPoint ProjectPoint(const FPoint& InPoint, FPoint* OutProjectedPoint = nullptr) const;// override;
		virtual void ProjectPoints(const TArray<FPoint>& InPoints, TArray<FPoint>* OutProjectedPointCoordinates, TArray<FPoint>* OutProjectedPoints = nullptr) const;// override;

		virtual void Presample(const FSurfacicBoundary& InBoundaries, FCoordinateGrid& OutCoordinates) override
		{
			OutCoordinates[EIso::IsoU].Empty(3);
			OutCoordinates[EIso::IsoU].Add(InBoundaries.UVBoundaries[EIso::IsoU].Min);
			OutCoordinates[EIso::IsoU].Add(InBoundaries.UVBoundaries[EIso::IsoU].GetMiddle());
			OutCoordinates[EIso::IsoU].Add(InBoundaries.UVBoundaries[EIso::IsoU].Max);

			OutCoordinates[EIso::IsoV].Empty(3);
			OutCoordinates[EIso::IsoV].Add(InBoundaries.UVBoundaries[EIso::IsoV].Min);
			OutCoordinates[EIso::IsoV].Add(InBoundaries.UVBoundaries[EIso::IsoV].GetMiddle());
			OutCoordinates[EIso::IsoV].Add(InBoundaries.UVBoundaries[EIso::IsoV].Max);
		}

		virtual void IsSurfaceClosed(bool& bOutClosedAlongU, bool& bOutClosedAlongV) const override
		{
			bOutClosedAlongU = false;
			bOutClosedAlongV = false;
		}

	private:
		virtual const void ComputeIsoTolerances() const override;

	};

} // namespace CADKernel
