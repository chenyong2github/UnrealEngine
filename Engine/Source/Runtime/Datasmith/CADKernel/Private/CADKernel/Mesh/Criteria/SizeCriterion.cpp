// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Mesh/Criteria/SizeCriterion.h"

#include "CADKernel/Core/KernelParameters.h"
#include "CADKernel/Core/System.h"
#include "CADKernel/Geo/GeoPoint.h"
#include "CADKernel/Topo/TopologicalEdge.h"

using namespace CADKernel;

FSizeCriterion::FSizeCriterion(double InSize, ECriterion InType)
	: FCriterion(InType)
	, Size(InSize)
{
}

FSizeCriterion::FSizeCriterion(FCADKernelArchive& Archive, ECriterion InType)
	: FCriterion(InType)
{
	Serialize(Archive);
}


void FSizeCriterion::ApplyOnEdgeParameters(TSharedRef<FTopologicalEdge> Edge, const TArray<double>& Coordinates, const TArray<FCurvePoint>& Points) const
{
	double NumericPrecision = Edge->GetTolerance3D();
	if (Edge->Length() <= NumericPrecision)
	{
		return;
	}

	switch (CriterionType)
	{
	case ECriterion::MaxSize:
		ApplyOnEdgeParameters(Edge, Coordinates, Points, Edge->GetDeltaUMaxs(), [](double NewValue, double& AbacusValue)
			{
				if (NewValue < AbacusValue)
				{
					AbacusValue = NewValue;
				}
			});
		break;

	case ECriterion::MinSize:
		ApplyOnEdgeParameters(Edge, Coordinates, Points, Edge->GetDeltaUMins(), [](double NewValue, double& AbacusValue)
			{
				if (NewValue > AbacusValue)
				{
					AbacusValue = NewValue;
				}
			});
		break;

	default:
		break;
	}
}

void FSizeCriterion::ApplyOnEdgeParameters(TSharedRef<FTopologicalEdge> Edge, const TArray<double>& Coordinates, const TArray<FCurvePoint>& Points, TArray<double>& DeltaUArray, TFunction<void(double, double&)> Compare) const
{
	double DeltaUMax = Coordinates[Coordinates.Num() - 1] - Coordinates[0];

	for (int32 PIndex = 1; PIndex < Coordinates.Num(); PIndex++)
	{
		double DeltaU = Coordinates[PIndex] - Coordinates[PIndex - 1];
		double Length = Points[2 * (PIndex - 1)].Point.Distance(Points[2 * PIndex].Point);

		DeltaU = (Length > 0) ? DeltaU * Size / Length : DeltaUMax;
		Compare(DeltaU, DeltaUArray[PIndex - 1]);
	}
}

void FSizeCriterion::UpdateDelta(double InDeltaU, double InUSag, double InDiagonalSag, double InVSag, double ChordLength, double DiagonalLength, double& OutSagDeltaUMax, double& OutSagDeltaUMin, FIsoCurvature& SurfaceCurvature) const
{
	if (ChordLength < KINDA_SMALL_NUMBER)
	{
		return;
	}

	switch (CriterionType)
	{
	case ECriterion::MaxSize:
		{
			double DeltaU = InDeltaU * Size / ChordLength;
			if (DeltaU < OutSagDeltaUMax)
			{
				OutSagDeltaUMax = DeltaU;
			}
		}
		break;

	case ECriterion::MinSize:
		{
			double DeltaU = InDeltaU * Size / ChordLength;
			if (DeltaU > OutSagDeltaUMin)
			{
				OutSagDeltaUMin = DeltaU;
			}
		}
		break;

	default:
		break;
	}
}

