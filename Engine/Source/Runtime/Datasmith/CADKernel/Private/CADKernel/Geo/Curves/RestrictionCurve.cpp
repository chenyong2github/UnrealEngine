// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Geo/Curves/RestrictionCurve.h"

#include "CADKernel/Math/SlopeUtils.h"

using namespace CADKernel;


#ifdef CADKERNEL_DEV
FInfoEntity& FRestrictionCurve::GetInfo(FInfoEntity& Info) const
{
	return FSurfacicCurve::GetInfo(Info)
		.Add(TEXT("2D polyline"), Polyline.Points2D)
		.Add(TEXT("3D polyline"), Polyline.Points3D);
}
#endif

void FRestrictionCurve::ExtendTo2DPoint(const FPoint2D& Point)
{
	Curve2D->ExtendTo(Point);
	if(Polyline.bWithTangent)
	{
		EvaluateSurfacicPolylineWithNormalAndTangent(Polyline);
	}
	else
	{
		EvaluateSurfacicPolyline(Polyline);
	}
}
