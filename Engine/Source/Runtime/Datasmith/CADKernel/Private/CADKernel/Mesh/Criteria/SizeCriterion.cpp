// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Mesh/Criteria/SizeCriterion.h"

#include "CADKernel/Core/KernelParameters.h"
#include "CADKernel/Core/System.h"
#include "CADKernel/Geo/GeoPoint.h"
#include "CADKernel/Topo/TopologicalEdge.h"

namespace UE::CADKernel
{

/**
 * In some cases as a huge plan, the MaxSizeCriteria can generate a lot of triangles e.g. a 100m side plan with a MaxSizeCriteria of 3cm will need 3e3 elements by side so 2e7 triangles.
 * This kind of case is most of the time a forgotten sketch body than a real wanted body. This mesh could make the process extremely long or simply crash all the system.
 * The idea is to not cancel the mesh of the body in the case it will really expected but to avoid the generation of a huge mesh with unwanted hundreds of millions of triangles.
 * So if MaxSizeCriteria will generate a huge mesh, this criteria is abandoned.
 * The chosen limit value is 3000 elements by side
 */
constexpr int32 GetMaxElementCountPerSide()
{
	return 3000;
}

void FMinSizeCriterion::ApplyOnEdgeParameters(FTopologicalEdge& Edge, const TArray<double>& Coordinates, const TArray<FCurvePoint>& Points) const
{
	double NumericPrecision = Edge.GetTolerance3D();
	if (Edge.Length() <= NumericPrecision)
	{
		return;
	}

	ApplyOnParameters(Coordinates, Points, Edge.GetDeltaUMins(), [](double NewValue, double& AbacusValue)
		{
			if (NewValue > AbacusValue)
			{
				AbacusValue = NewValue;
			}
		});
}


void FMaxSizeCriterion::ApplyOnEdgeParameters(FTopologicalEdge& Edge, const TArray<double>& Coordinates, const TArray<FCurvePoint>& Points) const
{
	double NumericPrecision = Edge.GetTolerance3D();
	if (Edge.Length() <= NumericPrecision)
	{
		return;
	}

	const double ElementCount = Edge.Length() / Size;
	if (ElementCount > GetMaxElementCountPerSide())
	{
		return;
	}

	ApplyOnParameters(Coordinates, Points, Edge.GetDeltaUMaxs(), [](double NewValue, double& AbacusValue)
		{
			if (NewValue < AbacusValue)
			{
				AbacusValue = NewValue;
			}
		});
}

void FSizeCriterion::ApplyOnParameters(const TArray<double>& Coordinates, const TArray<FCurvePoint>& Points, TArray<double>& DeltaUArray, TFunction<void(double, double&)> Compare) const
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

void FMinSizeCriterion::UpdateDelta(double InDeltaU, double InUSag, double InDiagonalSag, double InVSag, double ChordLength, double DiagonalLength, double& OutSagDeltaUMax, double& OutSagDeltaUMin, FIsoCurvature& SurfaceCurvature) const
{
	if (ChordLength < DOUBLE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	double DeltaU = InDeltaU * Size / ChordLength;
	if (DeltaU > OutSagDeltaUMin)
	{
		OutSagDeltaUMin = DeltaU;
	}
}

void FMaxSizeCriterion::UpdateDelta(double InDeltaU, double InUSag, double InDiagonalSag, double InVSag, double ChordLength, double DiagonalLength, double& OutSagDeltaUMax, double& OutSagDeltaUMin, FIsoCurvature& SurfaceCurvature) const
{
	if (ChordLength < DOUBLE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	const double ElementCount = ChordLength / Size;
	if (ElementCount > GetMaxElementCountPerSide())
	{
		return;
	}

	double DeltaU = InDeltaU * Size / ChordLength;
	if (DeltaU < OutSagDeltaUMax)
	{
		OutSagDeltaUMax = DeltaU;
	}
}

} // namespace UE::CADKernel
