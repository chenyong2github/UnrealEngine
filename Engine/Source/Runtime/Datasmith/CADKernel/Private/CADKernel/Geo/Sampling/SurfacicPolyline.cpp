// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Geo/Sampling/SurfacicPolyline.h"

#include "CADKernel/Core/KernelParameters.h"
#include "CADKernel/Core/System.h"
#include "CADKernel/Geo/Curves/Curve.h"
#include "CADKernel/Geo/Surfaces/Surface.h"
#include "CADKernel/Geo/Sampler/SamplerOnParam.h"

using namespace CADKernel;

FSurfacicPolyline::FSurfacicPolyline(TSharedRef<FSurface> InCarrierSurface, TSharedRef<FCurve> Curve2D, const double InTolerance)
	: bWithNormals(true)
	, bWithTangent(false)
{
	FSurfacicCurveSamplerOnParam Sampler(InCarrierSurface.Get(), Curve2D.Get(), Curve2D->GetBoundary(), InTolerance, InTolerance, *this);
	Sampler.Sample();
}

FSurfacicPolyline::FSurfacicPolyline(TSharedRef<FSurface> InCarrierSurface, TSharedRef<FCurve> Curve2D, const double ChordTolerance, const double ParamTolerance, bool bInWithNormals, bool bInWithTangents)
	: bWithNormals(bInWithNormals)
	, bWithTangent(bInWithTangents)
{
	FSurfacicCurveSamplerOnParam Sampler(InCarrierSurface.Get(), Curve2D.Get(), Curve2D->GetBoundary(), ChordTolerance, ParamTolerance, *this);
	Sampler.Sample();
}

void FSurfacicPolyline::CheckIfDegenerated(const double Tolerance3D, const double Tolerance2D, const FLinearBoundary& Boudary, bool& bDegeneration2D, bool& bDegeneration3D, double& Length3D) const
{
	TPolylineApproximator<FPoint2D> Approximator(Coordinates, Points2D);
	double Length2D = Approximator.ComputeLengthOfSubPolyline(Boudary);
	if (FMath::IsNearlyZero(Length2D, FMath::Max((double)KINDA_SMALL_NUMBER, Tolerance2D)))
	{
		bDegeneration2D = true;
		bDegeneration3D = true;
		Length3D = 0.;
		return;
	}

	bDegeneration2D = false;

	TPolylineApproximator<FPoint> Approximator3D(Coordinates, Points3D);
	Length3D = Approximator3D.ComputeLengthOfSubPolyline(Boudary);

	if (FMath::IsNearlyZero(Length3D, FMath::Max((double)KINDA_SMALL_NUMBER, Tolerance3D)))
	{
		bDegeneration3D = true;
	}
	else
	{
		bDegeneration3D = false;
	}
}

