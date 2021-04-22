// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Mesh/Criteria/Criterion.h"

/**
 * Sag & Angle criterion.pdf
 * https://docs.google.com/presentation/d/1bUnrRFWCW3sDn9ngb9ftfQS-2JxNJaUZlh783hZMMEw/edit?usp=sharing
*/

namespace CADKernel
{
	struct FCurvePoint;

	class FSizeCriterion : public FCriterion
	{
		friend class FEntity;
	
	protected:
		double Size;

		FSizeCriterion(double Size, ECriterion Type);
		FSizeCriterion(FCADKernelArchive& Archive, ECriterion InCriterionType);

	public:

		void Serialize(FCADKernelArchive& Ar)
		{
			FCriterion::Serialize(Ar);
			Ar << Size;
		}

		void ApplyOnEdgeParameters(TSharedRef<FTopologicalEdge> Edge, const TArray<double>& Coordinates, const TArray<FCurvePoint>& Points) const override;

		double Value() const override
		{
			return Size;
		}

		static double DefaultValue(ECriterion Type)
		{
			switch (Type)
			{
			case ECriterion::MinSize:
				return 0.1;
			case ECriterion::MaxSize:
				return 30;
			}
			return 0;
		}

		void UpdateDelta(double InDeltaU, double InUSag, double InDiagonalSag, double InVSag, double ChordLength, double DiagonalLength, double& OutSagDeltaUMax, double& OutSagDeltaUMin, FIsoCurvature& SurfaceCurvature) const override;

		void ApplyOnEdgeParameters(TSharedRef<FTopologicalEdge> Edge, const TArray<double>& TabU, const TArray<FCurvePoint>& tabPt, TArray<double>& tabDeltaU, TFunction<void(double, double&)> Compare) const;
	};

} // namespace CADKernel

