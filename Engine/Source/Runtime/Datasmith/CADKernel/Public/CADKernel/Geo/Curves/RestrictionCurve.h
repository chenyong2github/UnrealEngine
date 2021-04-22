// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Geo/Curves/SurfacicCurve.h"
#include "CADKernel/Geo/Sampling/SurfacicPolyline.h"

namespace CADKernel
{
	/**
	 * A restriction curve is the curve carrying an edge
	 * 
	 * It's defined by
	 * - A surfacic curve defined by a 2D curve (@see Curve2D) and the carrier surface (@see CarrierSurface) of the TopologicalFace containing the edge:
	 * - A linear approximation of the surfacic curve (@see Polyline) respecting the System Geometrical Tolerance (@see FKernelParameters::GeometricalTolerance)
	 *   The linear approximation is:
	 *      - an array of increasing coordinates (@see Polyline::Coordinates)
	 *      - an array of 2D Points relating to coordinates: points of curve in the parametric space of the carrier surface (@see Polyline::Points2D)
	 *      - an array of 3D Points relating to coordinates: 3D points of curve (@see Polyline::Points3D)
	 *      - an array of Normal relating to coordinates: Surface's normal (@see Polyline::Normal)
	 */
	class CADKERNEL_API FRestrictionCurve : public FSurfacicCurve
	{
		friend class FEntity;
		friend class FTopologicalEdge;

	protected:

		FSurfacicPolyline Polyline;

		FRestrictionCurve(const double InTolerance, TSharedRef<FSurface> InCarrierSurface, TSharedRef<FCurve> InCurve2D)
			: FSurfacicCurve(InTolerance, InCurve2D, InCarrierSurface)
			, Polyline(InCarrierSurface, InCurve2D, InTolerance)
		{
		}

		FRestrictionCurve(FCADKernelArchive& Archive)
			: FSurfacicCurve()
		{
			Serialize(Archive);
		}

	public:

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FSurfacicCurve::Serialize(Ar);
			Polyline.Serialize(Ar);
		}

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity&) const override;
#endif

		virtual ECurve GetCurveType() const override
		{
			return ECurve::Restriction;
		}

		const TSharedPtr<FCurve>& Get2DCurve() const
		{
			return Curve2D;
		}

		const TSharedPtr<FSurface>& GetCarrierSurface() const
		{
			return CarrierSurface;
		}

		TSharedPtr<FEntityGeom> ApplyMatrix(const FMatrixH& InMatrix) const override
		{
			ensureCADKernel(false);
			return TSharedPtr<FEntityGeom>();
		}

		/**
		 * Fast computation of the point in the parametric space of the carrier surface.
		 * This approximation is based on FSurfacicPolyline::Polyline 
		 */
		FPoint2D Approximate2DPoint(double InCoordinate) const
		{
			return Polyline.Approximate2DPoint(InCoordinate);
		}

		/**
		 * Fast computation of the point in the parametric space of the carrier surface.
		 * This approximation is based on FSurfacicPolyline::Polyline
		 */
		FPoint Approximate3DPoint(double InCoordinate) const
		{
			return Polyline.Approximate3DPoint(InCoordinate);
		}

		/**
		 * Fast computation of the point in the parametric space of the carrier surface.
		 * This approximation is based on FSurfacicPolyline::Polyline
		 */
		void Approximate2DPoints(const TArray<double>& InCoordinates, TArray<FPoint2D>& OutPoints) const
		{
			return Polyline.Approximate2DPoints(InCoordinates, OutPoints);
		}

		/**
		 * Fast computation of the 3D point of the curve.
		 * This approximation is based on FSurfacicPolyline::Polyline
		 */
		void Approximate3DPoints(const TArray<double>& InCoordinates, TArray<FPoint>& OutPoints) const
		{
			return Polyline.Approximate3DPoints(InCoordinates, OutPoints);
		}

		/**
		 * Approximation of surfacic polyline (points 2d, 3d, normals, tangents) defined by its coordinates compute with carrier surface polyline
		 */
		void ApproximatePolyline(FSurfacicPolyline& OutPolyline) const
		{
			Polyline.ApproximatePolyline(OutPolyline);
		}


		double GetCoordinateOfProjectedPoint(const FLinearBoundary& InBoundary, const FPoint& PointOnEdge, FPoint& ProjectedPoint) const
		{
			return Polyline.GetCoordinateOfProjectedPoint(InBoundary, PointOnEdge, ProjectedPoint);
		}

		void ProjectPoints(const FLinearBoundary& InBoundary, const TArray<FPoint>& InPointsToProject, TArray<double>& ProjectedPointCoordinates, TArray<FPoint>& ProjectedPoints) const
		{
			Polyline.ProjectPoints(InBoundary, InPointsToProject, ProjectedPointCoordinates, ProjectedPoints);
		}

		/**
		 * Project a set of points of a twin curve on the 3D polyline and return the coordinate of the projected point
		 */
		void ProjectTwinCurvePoints(const TArray<FPoint>& InPointsToProject, bool bSameOrientation, TArray<double>& OutProjectedPointCoords) const
		{
			Polyline.ProjectCoincidentalPolyline(InPointsToProject, bSameOrientation, OutProjectedPointCoords);
		}

		/**
		 * A check is done to verify that:
		 * - the curve is degenerated in the parametric space of the carrier surface i.e. the 2D length of the curve is not nearly equal to zero
		 * - the curve is degenerated in 3D i.e. the 3D length of the curve is not nearly equal to zero
		 * 
		 * A curve can be degenerated in 3D and not in 2D in the case of locally degenerated carrier surface.
		 */
		void CheckIfDegenerated(const FLinearBoundary& Boudary, bool& bDegeneration2D, bool& bDegeneration3D, double& Length3D) const
		{
			double Tolerance2D = GetCarrierSurface()->Get2DTolerance();
			Polyline.CheckIfDegenerated(Tolerance, Tolerance2D, Boudary, bDegeneration2D, bDegeneration3D, Length3D);
		}

		/**
		 * @return the size of the polyline i.e. the count of points.
		 */
		int32 GetPolylineSize()
		{
			return Polyline.Size();
		}

		/**
		 * Get the sub polyline bounded by the input InBoundary in the orientation of the input InOrientation and append it to the output OutPoints
		 */
		template<class PointType>
		void GetDiscretizationPoints(const FLinearBoundary& Boundary, EOrientation Orientation, TArray<PointType>& OutPoints) const
		{
			Polyline.GetSubPolyline(Boundary, Orientation, OutPoints);
		}

		/**
		 * Samples the sub curve limited by the boundary respecting the input Desired segment length 
		 */
		void Sample(const FLinearBoundary& InBoundary, const double DesiredSegmentLength, TArray<double>& OutCoordinates) const
		{
			Polyline.Sample(InBoundary, DesiredSegmentLength, OutCoordinates);
		}

		virtual double ComputeLength(const FLinearBoundary& InBoundary) const
		{
			return Polyline.GetLength(InBoundary);
		}

		void ExtendTo2DPoint(const FPoint2D& Point);
	};

} // namespace CADKernel

