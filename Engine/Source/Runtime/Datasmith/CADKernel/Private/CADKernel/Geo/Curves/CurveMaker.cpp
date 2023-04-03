// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Geo/Curves/BezierCurve.h"
//#include "CADKernel/Geo/Curves/BoundedCurve.h"
//#include "CADKernel/Geo/Curves/CompositeCurve.h"
#include "CADKernel/Geo/Curves/Curve.h"
//#include "CADKernel/Geo/Curves/EllipseCurve.h"
//#include "CADKernel/Geo/Curves/HyperbolaCurve.h"
#include "CADKernel/Geo/Curves/NURBSCurve.h"
//#include "CADKernel/Geo/Curves/ParabolaCurve.h"
//#include "CADKernel/Geo/Curves/PolylineCurve.h"
//#include "CADKernel/Geo/Curves/RestrictionCurve.h"
//#include "CADKernel/Geo/Curves/SegmentCurve.h"
#include "CADKernel/Geo/Curves/SplineCurve.h"
//#include "CADKernel/Geo/Curves/SurfacicCurve.h"

namespace UE::CADKernel
{

TSharedPtr<FCurve> FCurve::MakeNurbsCurve(FNurbsCurveData& InNurbsData)
{
	return FEntity::MakeShared<UE::CADKernel::FNURBSCurve>(InNurbsData);
}

TSharedPtr<FCurve> FCurve::MakeBezierCurve(const TArray<FPoint>& InPoles)
{
	return FEntity::MakeShared<UE::CADKernel::FBezierCurve>(InPoles);
}

TSharedPtr<FCurve> FCurve::MakeSplineCurve(const TArray<FPoint>& InPoles)
{
	return FEntity::MakeShared<UE::CADKernel::FSplineCurve>(InPoles);
}

TSharedPtr<FCurve> FCurve::MakeSplineCurve(const TArray<FPoint>& InPoles, const TArray<FPoint>& InTangents)
{
	return FEntity::MakeShared<UE::CADKernel::FSplineCurve>(InPoles, InTangents);
}

TSharedPtr<FCurve> FCurve::MakeSplineCurve(const TArray<FPoint>& InPoles, const TArray<FPoint>& InArriveTangents, const TArray<FPoint>& InLeaveTangents)
{
	return FEntity::MakeShared<UE::CADKernel::FSplineCurve>(InPoles, InArriveTangents, InLeaveTangents);
}

}