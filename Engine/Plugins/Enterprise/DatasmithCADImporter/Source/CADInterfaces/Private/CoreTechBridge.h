// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CADKernel/Core/Types.h"

#ifdef USE_KERNEL_IO_SDK 

#ifdef CADKERNEL_DEV
#include "CADKernel/Geo/Surfaces/Surface.h"
#endif
#include "CADKernel/Math/MatrixH.h"

#pragma warning(push)
#pragma warning(disable:4996) // unsafe sprintf
#pragma warning(disable:4828) // illegal character
#include "kernel_io/kernel_io.h"
#include "kernel_io/object_io/object_io.h"
#pragma warning(pop)

namespace CADKernel
{
	struct FSurfacicBoundary;

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

	class FCoreTechBridge
	{
	private:

		TSharedRef<FSession> Session;
		const double GeometricTolerance;
		const double SquareGeometricTolerance;
		const double SquareJoiningVertexTolerance;

		TMap<const uint32, TSharedPtr<FEntity>> CTIdToEntity;

		TArray<TSharedPtr<FCriterion>> Criteria;

	public:
		FCoreTechBridge(TSharedRef<FSession>& InSession);

		TSharedRef<FBody> AddBody(CT_OBJECT_ID CTBodyId);

		static FString AsFString(CT_STR CtName)
		{
			return CtName.IsEmpty() ? FString() : CtName.toUnicode();
		};

	private:
		void AddFace(CT_OBJECT_ID CTFaceId, TSharedRef<FShell>& Shell);

		TSharedPtr<FTopologicalLoop> AddLoop(CT_OBJECT_ID CTLoopId, TSharedRef<FSurface>& Surface);
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

		void AddMetadata(CT_OBJECT_ID NodeId, TSharedRef<FMetadataDictionary> Entity);

		template<typename EntityType>
		void AddMetadata(CT_OBJECT_ID NodeId, TSharedRef<EntityType> Entity)
		{
			AddMetadata(NodeId, (TSharedRef<FMetadataDictionary>) Entity);
		}

#ifdef CADKERNEL_DEV
		TMap<const uint32, FMatrixH> SurfaceToMatrix;

		const FMatrixH& GetParamSpaceTransform(TSharedPtr<FSurface>& Surface)
		{
			FMatrixH* Matrix = SurfaceToMatrix.Find(Surface->GetId());
			return Matrix == nullptr ? FMatrixH::Identity : *Matrix;
		}

		void SetParamSpaceTransorm(TSharedPtr<FSurface>& Surface, const FMatrixH Matrix)
		{
			SurfaceToMatrix.Add(Surface->GetId(), Matrix);
		}
#endif

	};
}

#endif // USE_KERNEL_IO_SDK
