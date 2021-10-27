// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Geo/Curves/RestrictionCurve.h"

#include "CADKernel/Math/SlopeUtils.h"


#ifdef CADKERNEL_DEV
CADKernel::FInfoEntity& CADKernel::FRestrictionCurve::GetInfo(FInfoEntity& Info) const
{
	return FSurfacicCurve::GetInfo(Info)
		.Add(TEXT("2D polyline"), Polyline.Points2D)
		.Add(TEXT("3D polyline"), Polyline.Points3D);
}
#endif

void CADKernel::FRestrictionCurve::ExtendTo(const FPoint2D& Point)
{
	Curve2D->ExtendTo(Point);
	EvaluateSurfacicPolyline(Polyline);
}
