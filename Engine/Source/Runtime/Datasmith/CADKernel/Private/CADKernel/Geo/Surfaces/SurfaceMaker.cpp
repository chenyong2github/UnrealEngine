// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Geo/Surfaces/Surface.h"

#include "CADKernel/Geo/Surfaces/PlaneSurface.h"
#include "CADKernel/Geo/Surfaces/BezierSurface.h"
#include "CADKernel/Geo/Surfaces/NURBSSurface.h"
#include "CADKernel/Geo/Surfaces/RuledSurface.h"
#include "CADKernel/Geo/Surfaces/RevolutionSurface.h"
#include "CADKernel/Geo/Surfaces/TabulatedCylinderSurface.h"
#include "CADKernel/Geo/Surfaces/CylinderSurface.h"
#include "CADKernel/Geo/Surfaces/OffsetSurface.h"
#include "CADKernel/Geo/Surfaces/CompositeSurface.h"
#include "CADKernel/Geo/Surfaces/CoonsSurface.h"
#include "CADKernel/Geo/Surfaces/SphericalSurface.h"
#include "CADKernel/Geo/Surfaces/TorusSurface.h"
#include "CADKernel/Geo/Surfaces/ConeSurface.h"


namespace CADKernel
{
TSharedPtr<FSurface> FSurface::MakeBezierSurface(const double InToleranceGeometric, int32 InUDegre, int32 InVDegre, const TArray<FPoint>& InPoles)
{
	return CADKernel::FEntity::MakeShared<CADKernel::FBezierSurface>(InToleranceGeometric, InUDegre, InVDegre, InPoles);
}

TSharedPtr<FSurface> FSurface::MakeConeSurface(const double InToleranceGeometric, const FMatrixH& InMatrix, double InStartRadius, double InConeAngle, const FSurfacicBoundary& InBoundary)
{
	return CADKernel::FEntity::MakeShared<CADKernel::FConeSurface>(InToleranceGeometric, InMatrix, InStartRadius, InConeAngle, InBoundary);
}

TSharedPtr<FSurface> FSurface::MakeCylinderSurface(const double InToleranceGeometric, const FMatrixH& InMatrix, const double InRadius, const FSurfacicBoundary& InBoundary)
{
	return CADKernel::FEntity::MakeShared<CADKernel::FCylinderSurface>(InToleranceGeometric, InMatrix, InRadius, InBoundary);
}

TSharedPtr<FSurface> FSurface::MakeNurbsSurface(const double InToleranceGeometric, const FNurbsSurfaceHomogeneousData& NurbsData)
{
	return CADKernel::FEntity::MakeShared<CADKernel::FNURBSSurface>(InToleranceGeometric, NurbsData);
}

TSharedPtr<FSurface> FSurface::MakeNurbsSurface(const double InToleranceGeometric, const FNurbsSurfaceData& NurbsData)
{
	return CADKernel::FEntity::MakeShared<CADKernel::FNURBSSurface>(InToleranceGeometric, NurbsData);
}

TSharedPtr<FSurface> FSurface::MakePlaneSurface(const double InToleranceGeometric, const FMatrixH& InMatrix, const FSurfacicBoundary& InBoundary)
{
	return CADKernel::FEntity::MakeShared<CADKernel::FPlaneSurface>(InToleranceGeometric, InMatrix, InBoundary);
}

TSharedPtr<FSurface> FSurface::MakeSphericalSurface(const double InToleranceGeometric, const FMatrixH& InMatrix, double InRadius, const FSurfacicBoundary& InBoundary)
{
	return CADKernel::FEntity::MakeShared<CADKernel::FSphericalSurface>(InToleranceGeometric, InMatrix, InRadius, InBoundary);
}

TSharedPtr<FSurface> FSurface::MakeTorusSurface(const double InToleranceGeometric, const FMatrixH& InMatrix, double InMajorRadius, double InMinorRadius, const FSurfacicBoundary& InBoundary)
{
	return CADKernel::FEntity::MakeShared<CADKernel::FTorusSurface>(InToleranceGeometric, InMatrix, InMajorRadius, InMinorRadius, InBoundary);
}

} // namespace CADKernel

