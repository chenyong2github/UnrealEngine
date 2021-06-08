// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Geo/Surfaces/Surface.h"
#include "CADKernel/Math/BSpline.h"

namespace CADKernel
{

	class CADKERNEL_API FNURBSSurface : public FSurface
	{
		friend FEntity;

	protected:
		int32 PoleUNum;
		int32 PoleVNum;

		int32 UDegree;
		int32 VDegree;

		TArray<double> UNodalVector;
		TArray<double> VNodalVector;

		TArray<double> Weights;

		TArray<FPoint> Poles;

		bool bIsRational;

		/**
		 * Data generated at initialization which are not serialized
		 */
		TArray<double> HomogeneousPoles;

		/**
		 * Build a Non uniform B-Spline surface
		 * @param NodalVectorU Its size is the number of poles in U + the surface degree in U + 1 (PoleUNum + UDegre + 1)
		 * @param NodalVectorV Its size is the number of poles in V + the surface degree in V + 1 (PoleVNum + VDegre + 1)
		 */
		FNURBSSurface(const double InToleranceGeometric, int32 InPoleUNum, int32 InPoleVNum, int32 InDegreU, int32 InDegreV, const TArray<double>& InNodalVectorU, const TArray<double>& InNodalVectorV, const TArray<FPoint>& InPoles)
			: FSurface(InToleranceGeometric)
			, PoleUNum(InPoleUNum)
			, PoleVNum(InPoleVNum)
			, UDegree(InDegreU)
			, VDegree(InDegreV)
			, UNodalVector(InNodalVectorU)
			, VNodalVector(InNodalVectorV)
			, Poles(InPoles)
			, bIsRational(false)
		{
			Finalize();
		}

		/**
		 * Build a Non uniform rational B-Spline surface
		 * @param NodalVectorU Its size is the number of poles in U + the surface degree in U + 1 (PoleUNum + UDegre + 1)
		 * @param NodalVectorV Its size is the number of poles in V + the surface degree in V + 1 (PoleVNum + VDegre + 1)
		 */
		FNURBSSurface(const double InToleranceGeometric, int32 InPoleUNum, int32 InPoleVNum, int32 InDegreU, int32 InDegreV, const TArray<double>& InNodalVectorU, const TArray<double>& InNodalVectorV, const TArray<FPoint>& InPoles, const TArray<double>& InWeights)
			: FSurface(InToleranceGeometric)
			, PoleUNum(InPoleUNum)
			, PoleVNum(InPoleVNum)
			, UDegree(InDegreU)
			, VDegree(InDegreV)
			, UNodalVector(InNodalVectorU)
			, VNodalVector(InNodalVectorV)
			, Weights(InWeights)
			, Poles(InPoles)
			, bIsRational(true)
		{
			SetMinToleranceIso();
			Finalize();
		}

		FNURBSSurface(FCADKernelArchive& Archive)
			: FSurface()
		{
			Serialize(Archive);
			Finalize();
		}

	public:

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FSurface::Serialize(Ar);
			Ar << PoleUNum;
			Ar << PoleVNum;
			Ar << UDegree;
			Ar << VDegree;
			Ar.Serialize(UNodalVector);
			Ar.Serialize(VNodalVector);
			Ar.Serialize(Weights);
			Ar.Serialize(Poles);
			Ar << bIsRational;
		}

		ESurface GetSurfaceType() const
		{
			return ESurface::Nurbs;
		}

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity&) const override;
#endif

		constexpr const int32 GetDegree(EIso Iso) const
		{
			switch (Iso)
			{
			case EIso::IsoU:
				return UDegree;
			case EIso::IsoV:
			default:
				return VDegree;
			}
		}

		constexpr const int32 GetPoleCount(EIso Iso) const
		{
			switch (Iso)
			{
			case EIso::IsoU:
				return PoleUNum;
			case EIso::IsoV:
			default:
				return PoleVNum;
			}
		}

		const TArray<FPoint>& GetPoles() const
		{
			return Poles;
		}

		const TArray<double>& GetWeights() const
		{
			return Weights;
		}

		TArray<double> GetHPoles() const
		{
			return HomogeneousPoles;
		}

		constexpr const TArray<double>& GetNodalVector(EIso Iso) const
		{
			switch (Iso)
			{
			case EIso::IsoU:
				return UNodalVector;
			case EIso::IsoV:
			default:
				return VNodalVector;
			}
		}

		bool IsRational() const
		{
			return bIsRational;
		}


		virtual TSharedPtr<FEntityGeom> ApplyMatrix(const FMatrixH& InMatrix) const override;

		virtual void EvaluatePoint(const FPoint2D& InSurfacicCoordinate, FSurfacicPoint& OutPoint3D, int32 InDerivativeOrder = 0) const override
		{
			BSpline::EvaluatePoint(*this, InSurfacicCoordinate, OutPoint3D, InDerivativeOrder);
		}

		void EvaluatePointGrid(const FCoordinateGrid& InSurfacicCoordinates, FSurfacicSampling& OutPoints, bool bComputeNormals) const override
		{
			BSpline::EvaluatePointGrid(*this, InSurfacicCoordinates, OutPoints, bComputeNormals);
		}

		virtual void LinesNotDerivables(const FSurfacicBoundary& InBoundary, int32 InDerivativeOrder, FCoordinateGrid& OutNotDerivableCoordinates) const override
		{
			BSpline::FindNotDerivableParameters(*this, InDerivativeOrder, InBoundary, OutNotDerivableCoordinates);
		}

	private:
		void Finalize();
	};

} // namespace CADKernel

