// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Geo/Curves/Curve.h"

namespace CADKernel
{
	class CADKERNEL_API FBezierCurve : public FCurve
	{
		friend class FEntity;

	protected:
		TArray<FPoint> Poles;

		FBezierCurve(const TArray<FPoint>& InPoles)
			: FCurve(3)
			, Poles{ InPoles }
		{
		}

		FBezierCurve(FCADKernelArchive& Archive)
			: FCurve(3)
		{
			Serialize(Archive);
		}

	public:

		virtual void Serialize(FCADKernelArchive& Ar) override
		{
			FCurve::Serialize(Ar);
			Ar.Serialize(Poles);
		}

#ifdef CADKERNEL_DEV
		virtual FInfoEntity& GetInfo(FInfoEntity&) const override;
#endif

		virtual ECurve GetCurveType() const override
		{
			return ECurve::Bezier;
		}

		int32 GetDegre() const
		{
			return Poles.Num() - 1;
		}

		const TArray<FPoint>& GetPoles() const
		{
			return Poles;
		}

		virtual TSharedPtr<FEntityGeom> ApplyMatrix(const FMatrixH& InMatrix) const override;

		virtual void EvaluatePoint(double Coordinate, FCurvePoint& OutPoint, int32 DerivativeOrder = 0) const override;

		virtual void ExtendTo(const FPoint& Point) override;

	};

} // namespace CADKernel

