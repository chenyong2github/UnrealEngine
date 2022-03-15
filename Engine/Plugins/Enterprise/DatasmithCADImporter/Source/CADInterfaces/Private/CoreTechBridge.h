// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CADKernel/Core/Types.h"

#ifdef USE_KERNEL_IO_SDK 

#include "CADFileReport.h"

#include "CADKernel/Math/MatrixH.h"

#pragma warning(push)
#pragma warning(disable:4996) // unsafe sprintf
#pragma warning(disable:4828) // illegal character
#include "kernel_io/kernel_io.h"
#include "kernel_io/object_io/object_io.h"
#pragma warning(pop)

namespace CADKernel
{
class FSurfacicBoundary;

class FBody;
class FCriterion;
class FCurve;
class FEntity;
class FFaceMesh;
class FMetadataDictionary;
class FModel;
class FPoint;
class FSession;
class FShell;
class FSurface;
class FTopologicalEdge;
class FTopologicalLoop;
class FTopologicalShapeEntity;

class FCoreTechBridge
{
private:

	FSession& Session;
	const double GeometricTolerance;
	const double SquareGeometricTolerance;
	const double SquareJoiningVertexTolerance;

	TMap<const uint32, TSharedPtr<FEntity>> CTIdToEntity;

	TArray<TSharedPtr<FCriterion>> Criteria;

	FCADFileReport& Report;

public:
	FCoreTechBridge(FSession& InSession, FCADFileReport& InReport);

	TSharedRef<FBody> AddBody(CT_OBJECT_ID CTBodyId);

	static FString AsFString(CT_STR CtName)
	{
		return CtName.IsEmpty() ? FString() : CtName.toUnicode();
	};

	void AddFace(CT_OBJECT_ID CTFaceId, TSharedRef<FShell>& Shell);

private:

	TSharedPtr<FTopologicalLoop> AddLoop(CT_OBJECT_ID CTLoopId, TSharedRef<FSurface>& Surface, const bool bIsExternalLoop);

	/**
	 * Build face's links with its neighbor have to be done after the loop is finalize.
	 * This is to avoid to link an edge with another and then to delete it...
	 */
	void LinkEdgesLoop(CT_OBJECT_ID CTLoopId, FTopologicalLoop& Loop);

	TSharedPtr<FTopologicalEdge> AddEdge(CT_OBJECT_ID CTCoedgeId, TSharedRef<FSurface>& Surface);

	TSharedPtr<FSurface> AddSurface(CT_OBJECT_ID CTSurfaceId, const FSurfacicBoundary& OutBoundary);
	TSharedPtr<FSurface> AddPlaneSurface(CT_OBJECT_ID CTSurfaceId, const FSurfacicBoundary& InBoundary);
	TSharedPtr<FSurface> AddNurbsSurface(CT_OBJECT_ID CTSurfaceId);
	TSharedPtr<FSurface> AddRevolutionSurface(CT_OBJECT_ID CTSurfaceId, const FSurfacicBoundary& InBoundary);
	TSharedPtr<FSurface> AddConeSurface(CT_OBJECT_ID CTSurfaceId, const FSurfacicBoundary& InBoundary);
	TSharedPtr<FSurface> AddOffsetSurface(CT_OBJECT_ID CTSurfaceId, const FSurfacicBoundary& InBoundary);
	TSharedPtr<FSurface> AddCylinderSurface(CT_OBJECT_ID CTSurfaceId, const FSurfacicBoundary& InBoundary);
	TSharedPtr<FSurface> AddSphereSurface(CT_OBJECT_ID CTSurfaceId, const FSurfacicBoundary& InBoundary);
	TSharedPtr<FSurface> AddTorusSurface(CT_OBJECT_ID CTSurfaceId, const FSurfacicBoundary& InBoundary);
	TSharedPtr<FSurface> AddRuledSurface(CT_OBJECT_ID CTSurfaceId);
	TSharedPtr<FSurface> AddLinearTransfoSurface(CT_OBJECT_ID CTSurfaceId);

	TSharedPtr<FCurve> AddCircleCurve(CT_OBJECT_ID CTCurveId);
	TSharedPtr<FCurve> AddCompositeCurve(CT_OBJECT_ID CTCurveId);
	TSharedPtr<FCurve> AddCurve(CT_OBJECT_ID CTCurveId, CT_OBJECT_ID CTSurfaceId = 0);
	TSharedPtr<FCurve> AddCurveOnSurface(CT_OBJECT_ID CTCurveId);
	TSharedPtr<FCurve> AddEllipseCurve(CT_OBJECT_ID CTCurveId);
	TSharedPtr<FCurve> AddHyperbolaCurve(CT_OBJECT_ID CTCurveId);
	TSharedPtr<FCurve> AddLineCurve(CT_OBJECT_ID CTCurveId, CT_OBJECT_ID CTSurfaceId = 0);
	TSharedPtr<FCurve> AddNurbsCurve(CT_OBJECT_ID CTCurveId);
	TSharedPtr<FCurve> AddParabolaCurve(CT_OBJECT_ID CTCurveId);

	void Get2DCurvesRange(CT_OBJECT_ID CTFaceId, FSurfacicBoundary& OutBoundary);

	FMatrixH CreateCoordinateSystem(const CT_COORDINATE& InOrigin, const CT_VECTOR& InDirection, const CT_VECTOR& InURef);

	void AddMetadata(CT_OBJECT_ID NodeId, FTopologicalShapeEntity& Entity);

};
}

#endif // USE_KERNEL_IO_SDK
