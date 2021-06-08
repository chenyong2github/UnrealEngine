// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Geo/Sampling/SurfacicPolyline.h"

#include "CADKernel/Core/KernelParameters.h"
#include "CADKernel/Core/System.h"
#include "CADKernel/Geo/Curves/Curve.h"
#include "CADKernel/Geo/Surfaces/Surface.h"
#include "CADKernel/Geo/Sampler/SamplerOnParam.h"
#include "CADKernel/Geo/Sampling/PolylineTools.h"

using namespace CADKernel;

FSurfacicPolyline::FSurfacicPolyline(TSharedRef<FSurface> InCarrierSurface, TSharedRef<FCurve> Curve2D, const double InTolerance)
	: bWithNormals(true)
	, bWithTangent(false)
{
	FSurfacicCurveSamplerOnParam Sampler(InCarrierSurface.Get(), Curve2D.Get(), Curve2D->GetBoundary(), InTolerance, InTolerance, *this);
	Sampler.Sample();
}

FSurfacicPolyline::FSurfacicPolyline(TSharedRef<FSurface> InCarrierSurface, TSharedRef<FCurve> Curve2D)
	: FSurfacicPolyline(InCarrierSurface, Curve2D, InCarrierSurface->Get3DTolerance())
{
}

FSurfacicPolyline::FSurfacicPolyline(TSharedRef<FSurface> InCarrierSurface, TSharedRef<FCurve> Curve2D, const double ChordTolerance, const double ParamTolerance, bool bInWithNormals, bool bInWithTangents)
	: bWithNormals(bInWithNormals)
	, bWithTangent(bInWithTangents)
{
	FSurfacicCurveSamplerOnParam Sampler(InCarrierSurface.Get(), Curve2D.Get(), Curve2D->GetBoundary(), ChordTolerance, ParamTolerance, *this);
	Sampler.Sample();
}

void FSurfacicPolyline::CheckIfDegenerated(const double Tolerance3D, const FSurfacicTolerance& ToleranceIso, const FLinearBoundary& Boudary, bool& bDegeneration2D, bool& bDegeneration3D, double& Length3D) const
{
	TPolylineApproximator<FPoint> Approximator(Coordinates, Points3D);
	int32 BoundaryIndices[2];
	Approximator.GetStartEndIndex(Boudary, BoundaryIndices);

	TPolylineApproximator<FPoint> Approximator3D(Coordinates, Points3D);
	Length3D = Approximator3D.ComputeLengthOfSubPolyline(BoundaryIndices, Boudary);

	if (!FMath::IsNearlyZero(Length3D, Tolerance3D))
	{
		bDegeneration3D = false;
		bDegeneration2D = false;
		return;
	}

	bDegeneration3D = true;
	Length3D = 0.;

	// Tolerance along Iso U/V is very costly to compute and not accurate.
	// To test if a curve is degenerated, its 2d bounding box is compute and its compare to the surface boundary along U and along V
	// Indeed, defining a Tolerance2D has no sense has the boundary length along an Iso could be very very huge compare to the boundary length along the other Iso like [[0, 1000] [0, 1]]
	// The tolerance along ans iso is the length of the boundary along this iso divide by 100 000: if the curve length in 3d is 10m, the tolerance is 0.01mm

	FAABB2D Aabb;
	TPolylineApproximator<FPoint2D> Approximator2D(Coordinates, Points2D);
	Approximator2D.ComputeBoundingBox(BoundaryIndices, Boudary, Aabb);

	bDegeneration2D = (Aabb.GetSize(0) < ToleranceIso[EIso::IsoU] && Aabb.GetSize(1) < ToleranceIso[EIso::IsoV]);
}

void FSurfacicPolyline::GetExtremities(const FLinearBoundary& InBoundary, const double Tolerance3D, const FSurfacicTolerance& MinToleranceIso, FSurfacicCurveExtremity& Extremities) const
{
	FDichotomyFinder Finder(Coordinates);
	const int32 StartIndex = Finder.Find(InBoundary.Min);
	const int32 EndIndex = Finder.Find(InBoundary.Max);

	Extremities[0].Point2D = PolylineTools::ComputePoint(Coordinates, Points2D, StartIndex, InBoundary.Min);
	Extremities[0].Point = PolylineTools::ComputePoint(Coordinates, Points3D, StartIndex, InBoundary.Min);
	Extremities[0].Tolerance = ComputeTolerance(Tolerance3D, MinToleranceIso, StartIndex);

	Extremities[1].Point2D = PolylineTools::ComputePoint(Coordinates, Points2D, EndIndex, InBoundary.Max);
	Extremities[1].Point = PolylineTools::ComputePoint(Coordinates, Points3D, EndIndex, InBoundary.Max);
	if (EndIndex == StartIndex)
	{
		Extremities[1].Tolerance = Extremities[0].Tolerance;
	}
	else
	{
		Extremities[1].Tolerance = ComputeTolerance(Tolerance3D, MinToleranceIso, EndIndex);
	}
}

