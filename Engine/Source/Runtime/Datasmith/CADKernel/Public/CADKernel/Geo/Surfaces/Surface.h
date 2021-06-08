// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/CADKernelArchive.h"
#include "CADKernel/Core/EntityGeom.h"

#include "CADKernel/Geo/GeoEnum.h"
#include "CADKernel/Geo/GeoPoint.h"
#include "CADKernel/Math/Boundary.h"
#include "CADKernel/Math/MatrixH.h"
#include "CADKernel/Math/Point.h"
#include "CADKernel/Utils/Cache.h"

namespace CADKernel
{
	class FGrid;
	class FInfoEntity;
	class FSurfacicPolyline;

	struct FCurvePoint;
	struct FCurvePoint2D;
	struct FSurfacicSampling;

	class CADKERNEL_API FSurface : public FEntityGeom
	{
		friend FEntity;

	protected:

		double Tolerance3D;

		/**
		 * Tolerance along Iso U/V is very costly to compute and not accurate.
		 * A first approximation is based on the surface boundary along U and along V
		 * Indeed, defining a Tolerance2D has no sense has the boundary length along an Iso could be very very huge compare to the boundary length along the other Iso like [[0, 1000] [0, 1]]
		 * The tolerance along an iso is the length of the boundary along this iso divided by 100 000: if the curve length in 3d is 10m, the tolerance is 0.01mm
		 * In the used, a local tolerance has to be estimated
		 */
		mutable TCache<FSurfacicTolerance> MinToleranceIso;
		mutable FSurfacicBoundary Boundary;

	private:


		virtual void InitBoundary() const
		{
			Boundary.Set();
		}

	protected:

		FSurface() = default;

		FSurface(double InToleranceGeometric)
			: FEntityGeom()
			, Tolerance3D(InToleranceGeometric)
		{

		}

		FSurface(double InToleranceGeometric, const FSurfacicBoundary& InBoundary)
			: FEntityGeom()
			, Tolerance3D(InToleranceGeometric)
			, Boundary(InBoundary)
		{
		}

		FSurface(double InToleranceGeometric, double UMin, double UMax, double VMin, double VMax)
			: FEntityGeom()
			, Tolerance3D(InToleranceGeometric)
			, Boundary(UMin, UMax, VMin, VMax)
		{
		}

		virtual void SetMinToleranceIso() const
		{
			MinToleranceIso.Set(Boundary[EIso::IsoU].ComputeMinimalTolerance(), Boundary[EIso::IsoV].ComputeMinimalTolerance());
		}

	public:

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			// Surface's type is serialize because it is used to instantiate the correct entity on deserialization (@see Deserialize(FCADKernelArchive& Archive)) 
			if (Ar.IsSaving())
			{
				ESurface SurfaceType = GetSurfaceType();
				Ar << SurfaceType;
			}
			FEntityGeom::Serialize(Ar);

			Ar << Tolerance3D;
			Ar << MinToleranceIso;
			Ar << Boundary;
		}

		/**
		 * Specific method for surface family to instantiate the correct derived class of FCurve
		 */
		static TSharedPtr<FSurface> Deserialize(FCADKernelArchive& Archive);

		virtual ~FSurface() = default;

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity&) const override;
#endif

		virtual EEntity GetEntityType() const override
		{
			return EEntity::Surface;
		}

		virtual ESurface GetSurfaceType() const = 0;

		const FSurfacicBoundary& GetBoundary() const
		{
			if (!Boundary.IsValid())
			{
				InitBoundary();
				SetMinToleranceIso();
			}
			return Boundary;
		};

		void ExtendBoundaryTo(const FSurfacicBoundary MaxLimit)
		{
			Boundary.ExtendTo(MaxLimit);
			SetMinToleranceIso();
		}

		virtual TSharedPtr<FEntityGeom> ApplyMatrix(const FMatrixH& InMatrix) const = 0;

		/**
		 * Tolerance along Iso U/V is very costly to compute and not accurate.
		 * A first approximation is based on the surface parametric space length along U and along V
		 * Indeed, defining a Tolerance2D has no sense has the boundary length along an Iso could be very very huge compare to the boundary length along the other Iso like [[0, 1000] [0, 1]]
		 * @see FBoundary::ComputeMinimalTolerance
		 * In the used, a local tolerance has to be estimated
		 */
		const FSurfacicTolerance& GetIsoTolerances() const
		{
			ensureCADKernel(MinToleranceIso.IsValid());
			return MinToleranceIso;
		}

		/**
		 * Return the minimum tolerance in the parametric space of the surface along the specified axis 
		 * With Tolerance3D = FSysteme.GeometricalTolerance
		 * @see FBoundary::ComputeMinimalTolerance
		 */
		const double& GetIsoTolerance(EIso Iso) const
		{
			ensureCADKernel(MinToleranceIso.IsValid());
			return ((FSurfacicTolerance) MinToleranceIso)[Iso];
		}

		const double Get3DTolerance()
		{
			return Tolerance3D;
		}


		virtual void EvaluatePoint(const FPoint2D& InSurfacicCoordinate, FSurfacicPoint& OutPoint3D, int32 InDerivativeOrder = 0) const = 0;

		virtual void EvaluatePoints(const TArray<FPoint2D>& InSurfacicCoordinates, TArray<FSurfacicPoint>& OutPoint3D, int32 InDerivativeOrder = 0) const;
		virtual void EvaluatePoints(const TArray<FCurvePoint2D>& InSurfacicCoordinates, TArray<FSurfacicPoint>& OutPoint3D, int32 InDerivativeOrder = 0) const;
		virtual void EvaluatePoints(const TArray<FCurvePoint2D>& InSurfacicCoordinates, TArray<FCurvePoint>& OutPoint3D, int32 InDerivativeOrder = 0) const;
		virtual void EvaluatePoints(FSurfacicPolyline& Polyline) const;
		virtual void EvaluatePoints(const TArray<FCurvePoint2D>& Points2D, FSurfacicPolyline& Polyline) const;

		virtual void EvaluatePointGrid(const FCoordinateGrid& Coordinates, FSurfacicSampling& OutPoints, bool bComputeNormals = false) const;
		void EvaluateGrid(FGrid& Grid) const;

		virtual void EvaluateNormals(const TArray<FPoint2D>& Points2D, TArray<FPoint>& Normals) const;

		/**
		 * Divide the parametric space in the desired number of regular subdivisions and compute the associated PointGrid
		 */
		void Sample(const FSurfacicBoundary& Bounds, int32 NumberOfSubdivisions[2], FSurfacicSampling& OutPointSampling) const;

		/**
		 * Divide the parametric space in the desired number of regular subdivisions and compute the associated CoordinateGrid
		 */
		void Sample(const FSurfacicBoundary& Boundary, int32 NumberOfSubdivisions[2], FCoordinateGrid& OutCoordinateSampling) const;

		/**
		 * Generate a pre-sampling of the surface saved in OutCoordinates.
		 * This sampling is light enough to allow a fast computation of the grid, precise enough to compute accurately meshing criteria
		 */
		virtual void Presample(const FSurfacicBoundary& InBoundaries, FCoordinateGrid& OutCoordinates);

		virtual void LinesNotDerivables(const FSurfacicBoundary& Bounds, int32 InDerivativeOrder, FCoordinateGrid& OutNotDerivableCoordinates) const
		{
			OutNotDerivableCoordinates.Empty();
		}

		void LinesNotDerivables(int32 InDerivativeOrder, FCoordinateGrid& OutNotDerivableCoordinates) const
		{
			LinesNotDerivables(Boundary, InDerivativeOrder, OutNotDerivableCoordinates);
		}

		/**
		 * A surface is closed along an iso axis if it's connected to itself e.g. the edge carried by the iso curve U = UMin is linked to the edge carried by the iso curve U = UMax
		 * A complete sphere, torus are example of surface closed along U and V
		 * A cylinder with circle section or a cone are example of surface closed along an iso
		 */
		virtual void IsSurfaceClosed(bool& bOutClosedAlongU, bool& bOutClosedAlongV) const
		{
			bOutClosedAlongU = false;
			bOutClosedAlongV = false;
		}

		void PresampleIsoCircle(const FSurfacicBoundary& InBoundaries, FCoordinateGrid& Coordinates, EIso Iso)
		{
			constexpr const double ThreeOverPi = 3 / PI;
			double Delta = InBoundaries.Length(Iso);
			int32 SampleCount = /*FMath::Max(3, */ (int32)(Delta * ThreeOverPi + 1)/*)*/;
			Delta /= SampleCount;

			Coordinates[Iso].Empty(SampleCount + 1);
			double Sample = InBoundaries[Iso].GetMin();
			for (int32 Index = 0; Index <= SampleCount; ++Index)
			{
				Coordinates[Iso].Add(Sample);
				Sample += Delta;
			}
		}

		/**
		 * This function return the scale of the input Axis.
		 * This function is useful to estimate tolerance when scales are defined in the Matrix
		 * @param InAxis a vetor of length 1
		 * @param InMatrix
		 * @param InOrigin = InMatrix*FPoint::ZeroPoint
		 */
		static double ComputeScaleAlongAxis(const FPoint& InAxis, const FMatrixH& InMatrix, const FPoint& InOrigin)
		{
			FPoint Point = InMatrix.Multiply(InAxis);
			double Length = InOrigin.Distance(Point);
			return Length;
		};


	};
} // namespace CADKernel

