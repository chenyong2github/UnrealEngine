// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Geo/Curves/Curve.h"
#include "CADKernel/Geo/Sampling/Polyline.h"
#include "CADKernel/Geo/Sampling/PolylineTools.h"

namespace CADKernel
{
	class FInfoEntity;
	class FSurface;

	template<class PointType, class PointCurveType>
	class CADKERNEL_API TPolylineCurve : public FCurve, public TPolyline<PointType>
	{
		friend class FEntity;
		friend class FPolylineTools;

	protected:
		
		TPolylineApproximator<PointType> Approximator;

		TPolylineCurve(const double InTolerance, const TArray<PointType>& InPoints, const TArray<double>& InCoordinates, int8 InDimension)
			: FCurve(InTolerance, Dimension)
			, Approximator(this->Coordinates, this->Points)
		{
			this->Coordinates = InCoordinates;
			this->Points = InPoints;
			ensureCADKernel(Coordinates[0] < Coordinates.Last());
			Boundary.Set(this->Coordinates.HeapTop(), this->Coordinates.Last());
		}

		TPolylineCurve(const double InTolerance, const TArray<PointType>& InPoints, int8 InDimension)
			: FCurve(InTolerance, Dimension)
			, TPolyline<PointType>(InPoints)
			, Approximator(this->Coordinates, this->Points)
		{
			this->Coordinates.Reserve(InPoints.Num());
			this->Coordinates.Add(0.);

			double CurvilineLength = 0;
			for (int32 iPoint = 1; iPoint < this->Points.Num(); iPoint++)
			{
				CurvilineLength += this->Points[iPoint].Distance(this->Points[iPoint - 1]);
				this->Coordinates.Add(CurvilineLength);
			}

			Boundary.Set(0, CurvilineLength);
		}

		TPolylineCurve(FCADKernelArchive& Archive)
			: FCurve()
			, Approximator(this->Coordinates, this->Points)
		{
			Serialize(Archive);
		}

	public:

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FCurve::Serialize(Ar);
			TPolyline<PointType>::Serialize(Ar);
		}

		virtual void EvaluateCurvesPoint(double InCoordinate, PointCurveType& OutPoint, int32 InDerivativeOrder = 0) const
		{
>>>> ORIGINAL //UE5/Main/Engine/Source/Runtime/Datasmith/CADKernel/Public/CADKernel/Geo/Curves/PolylineCurve.h#1
			Approximator.ApproximatePoint<PointCurveType>(InCoordinate, OutPoint, InDerivativeOrder);
==== THEIRS //UE5/Main/Engine/Source/Runtime/Datasmith/CADKernel/Public/CADKernel/Geo/Curves/PolylineCurve.h#2
			template Approximator.ApproximatePoints<PointCurveType>(InCoordinate, OutPoint, InDerivativeOrder);
==== YOURS //David.Lesage_YUL-UE5-Main/Engine/Source/Runtime/Datasmith/CADKernel/Public/CADKernel/Geo/Curves/PolylineCurve.h
			Approximator.ApproximatePoint(InCoordinate, OutPoint, InDerivativeOrder);
<<<<
		}

		PointType EvaluatePointAt(double InCoordinate) const
		{
			return Approximator.ApproximatePoint(InCoordinate);
		}

		virtual void EvaluateCurvesPoints(const TArray<double>& InCoordinates, TArray<PointCurveType>& OutPoints, int32 InDerivativeOrder = 0) const
		{
>>>> ORIGINAL //UE5/Main/Engine/Source/Runtime/Datasmith/CADKernel/Public/CADKernel/Geo/Curves/PolylineCurve.h#1
			Approximator.ApproximatePoints<PointCurveType>(InCoordinates, OutPoints, InDerivativeOrder);
==== THEIRS //UE5/Main/Engine/Source/Runtime/Datasmith/CADKernel/Public/CADKernel/Geo/Curves/PolylineCurve.h#2
			template Approximator.ApproximatePoints<PointCurveType>(InCoordinates, OutPoints, InDerivativeOrder);
==== YOURS //David.Lesage_YUL-UE5-Main/Engine/Source/Runtime/Datasmith/CADKernel/Public/CADKernel/Geo/Curves/PolylineCurve.h
			Approximator.ApproximatePoints(InCoordinates, OutPoints, InDerivativeOrder);
<<<<
		}

		virtual double ComputeSubLength(const FLinearBoundary& InBoundary) const
		{
			return Approximator.ComputeLengthOfSubPolyline(InBoundary);
		}

		const TArray<PointType>& GetPolylinePoints() const
		{
			return this->Points;
		}

		const TArray<double>& GetPolylineParameters() const
		{
			return this->Coordinates;
		}

		virtual void FindNotDerivableCoordinates(const FLinearBoundary& InBoundary, int32 DerivativeOrder, TArray<double>& OutNotDerivableCoordinates) const override
		{
			//TO DO
			ensureCADKernel(false);
		}

		void SetPoints(const TArray<PointType>& InPoints)
		{
			this->Points = InPoints;
			GlobalLength.Empty();
		}

		template<class PolylineType>
		TSharedPtr<FEntityGeom> ApplyMatrixImpl(const FMatrixH& InMatrix) const
		{
			TArray<PointType> NewPoints;
			NewPoints.Reserve(this->Points.Num());

			for (PointType Point : this->Points)
			{
				NewPoints.Emplace(InMatrix.Multiply(Point));
			}

			return FEntity::MakeShared<PolylineType>(Tolerance, NewPoints, this->Coordinates);
		}

		virtual void ExtendTo(const FPoint& DesiredPoint) override
		{
			PolylineTools::ExtendTo(this->Points, (PointType) DesiredPoint);
		}
	};

	class CADKERNEL_API FPolylineCurve : public TPolylineCurve<FPoint, FCurvePoint>
	{
		friend class FEntity;

	protected:
		FPolylineCurve(const double InTolerance, const TArray<FPoint>& InPoints, const TArray<double>& InCoordinates)
			: TPolylineCurve<FPoint, FCurvePoint>(InTolerance, InPoints, InCoordinates, 3)
		{
		}

		FPolylineCurve(const double InTolerance, const TArray<FPoint>& InPoints)
			: TPolylineCurve<FPoint, FCurvePoint>(InTolerance, InPoints, 3)
		{
		}

		FPolylineCurve(FCADKernelArchive& Archive)
			: TPolylineCurve<FPoint, FCurvePoint>(Archive)
		{
		}

	public:

		virtual ECurve GetCurveType() const override
		{
			return ECurve::Polyline3D;
		}

		virtual TSharedPtr<FEntityGeom> ApplyMatrix(const FMatrixH& InMatrix) const override
		{
			return ApplyMatrixImpl<FPolylineCurve>(InMatrix);
		}

		virtual FPoint EvaluatePoint(double InCoordinate) const override
		{
			return EvaluatePointAt(InCoordinate);
		} 

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity& Info) const override;
#endif

	};

	class CADKERNEL_API FPolyline2DCurve : public TPolylineCurve<FPoint2D, FCurvePoint2D>
	{
		friend class FEntity;

	protected:
		FPolyline2DCurve(const double InTolerance, const TArray<FPoint2D>& InPoints, const TArray<double>& InCoordinates)
			: TPolylineCurve<FPoint2D, FCurvePoint2D>(InTolerance, InPoints, InCoordinates, 3)
		{
		}

		FPolyline2DCurve(const double InTolerance, const TArray<FPoint2D>& InPoints)
			: TPolylineCurve<FPoint2D, FCurvePoint2D>(InTolerance, InPoints, 3)
		{
		}

		FPolyline2DCurve(FCADKernelArchive& Archive)
			: TPolylineCurve<FPoint2D, FCurvePoint2D>(Archive)
		{
		}

	public:

		virtual ECurve GetCurveType() const override
		{
			return ECurve::Polyline3D;
		}

		virtual TSharedPtr<FEntityGeom> ApplyMatrix(const FMatrixH& InMatrix) const override
		{
			return ApplyMatrixImpl<FPolyline2DCurve>(InMatrix);
		}

		virtual void Evaluate2DPoint(double InCoordinate, FCurvePoint2D& OutPoint, int32 InDerivativeOrder = 0) const override
		{
			EvaluateCurvesPoint(InCoordinate, OutPoint, InDerivativeOrder);
		}

		virtual FPoint2D Evaluate2DPoint(double InCoordinate) const override
		{
			return EvaluatePointAt(InCoordinate);
		}

		virtual void Evaluate2DPoints(const TArray<double>& InCoordinates, TArray<FCurvePoint2D>& OutPoints, int32 InDerivativeOrder = 0) const override
		{
			EvaluateCurvesPoints(InCoordinates, OutPoints, InDerivativeOrder);
		}

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity& Info) const override;
#endif

	}; 
	
} // namespace CADKernel

