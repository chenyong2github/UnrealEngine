// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Mesh/Meshers/ParametricMesher.h"

#include "CADKernel/Mesh/Criteria/CriteriaGrid.h"
#include "CADKernel/Mesh/Criteria/Criterion.h"
#include "CADKernel/Mesh/Meshers/IsoTriangulator.h"
#include "CADKernel/Mesh/Meshers/MesherTools.h"
#include "CADKernel/Mesh/Structure/EdgeMesh.h"
#include "CADKernel/Mesh/Structure/FaceMesh.h"
#include "CADKernel/Mesh/Structure/ModelMesh.h"
#include "CADKernel/Mesh/Structure/ThinZone2DFinder.h"
#include "CADKernel/Mesh/Structure/VertexMesh.h"
#include "CADKernel/Topo/Body.h"
#include "CADKernel/Topo/Model.h"
#include "CADKernel/Topo/Shell.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Utils/Util.h"

#ifndef CADKERNEL_DEV
#include "Async/ParallelFor.h"
#endif

#ifdef CADKERNEL_DEV
#include "CADKernel/Mesh/Meshers/MesherReport.h"
#endif

//#define DEBUG_MESH_EDGE
//#define DEBUG_GETPREFERREDUVCOORDINATESFROMNEIGHBOURS
namespace UE::CADKernel
{

FParametricMesher::FParametricMesher(FModelMesh& InMeshModel)
	: MeshModel(InMeshModel)
	, bThinZoneMeshing(false)
{
}

void FParametricMesher::MeshEntities(TArray<FTopologicalShapeEntity*>& InEntities)
{
	int32 FaceCount = 0;

	for (FTopologicalFace* Face : Faces)
	{
		if (Face == nullptr)
		{
			continue;
		}
		Face->SetMarker1();
	}

	// count faces
	for (FTopologicalShapeEntity* TopologicalEntity : InEntities)
	{
		if (TopologicalEntity == nullptr)
		{
			continue;
		}
		FaceCount += TopologicalEntity->FaceCount();
	}
	Faces.Reserve(Faces.Num() + FaceCount);

	for (FTopologicalFace* Face : Faces)
	{
		if (Face == nullptr)
		{
			continue;
		}
		Face->ResetMarkers();
	}

	// Get independent Faces and spread body's shells orientation
	for (FTopologicalShapeEntity* TopologicalEntity : InEntities)
	{
		if (TopologicalEntity == nullptr)
		{
			continue;
		}

		TopologicalEntity->SpreadBodyOrientation();
		TopologicalEntity->GetFaces(Faces);
	}

	for (FTopologicalFace* Face : Faces)
	{
		if (Face == nullptr)
		{
			continue;
		}
		Face->ResetMarkers();
	}

	PreMeshingTasks();
	MeshEntities();
}

void FParametricMesher::PreMeshingTasks()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ParametricMesher::PreMeshingTasks);

	FTimePoint StartTime = FChrono::Now();
	FTimePoint ApplyCriteriaStartTime = FChrono::Now();

	FProgress ProgressBar(Faces.Num() * 2, TEXT("Meshing Entities : Apply Surface Criteria"));

	const TArray<TSharedPtr<FCriterion>>& Criteria = GetMeshModel().GetCriteria();

	// ============================================================================================================
	//      Apply Surface Criteria
	// ============================================================================================================

	TArray<FTopologicalFace*>& LocalFaces = Faces;
#ifndef CADKERNEL_DEV
	ParallelFor(Faces.Num(), [&LocalFaces, &Criteria, &bThinZone = bThinZoneMeshing](int32 Index)
#else
	bool bThinZone = bThinZoneMeshing;
	for (int32 Index = 0; Index < Faces.Num(); ++Index)
#endif
	{
		FTopologicalFace* Face = LocalFaces[Index];
		if (Face == nullptr || Face->IsNotMeshable())
		{
			return;
		}
		ApplyFaceCriteria(*Face, Criteria, bThinZone);
		if (!Face->IsDeletedOrDegenerated())
		{
			Face->ComputeSurfaceSideProperties();
		}
	}
#ifndef CADKERNEL_DEV
	);
#endif

#ifdef CADKERNEL_DEV
	MesherReport.Chronos.ApplyCriteriaDuration = FChrono::Elapse(ApplyCriteriaStartTime);
#endif
}

void FParametricMesher::MeshEntities()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ParametricMesher::MeshEntities);

	FTimePoint StartTime = FChrono::Now();

	FProgress ProgressBar(Faces.Num() * 2, TEXT("Meshing Entities : Find quad surfaces"));

#ifdef CADKERNEL_DEV
	MesherReport.Chronos.ApplyCriteriaDuration = FChrono::Elapse(StartTime);
#endif

	FTimePoint MeshingStartTime = FChrono::Now();

	// ============================================================================================================
	//      Find and sort quad surfaces 
	// ============================================================================================================

	TArray<FCostToFace> QuadTrimmedSurfaceSet;

	if (Faces.Num() > 1)
	{
		TArray<FTopologicalFace*> OtherEntities;

		FMessage::Printf(Log, TEXT("  Isolate QuadPatch\n"));
		FTimePoint IsolateQuadPatchStartTime = FChrono::Now();

		IsolateQuadFace(QuadTrimmedSurfaceSet, OtherEntities);

#ifdef CADKERNEL_DEV
		MesherReport.Chronos.IsolateQuadPatchDuration = FChrono::Elapse(IsolateQuadPatchStartTime);
#endif

		FMessage::Printf(Log, TEXT("  %d Quad Surfaces found\n"), QuadTrimmedSurfaceSet.Num());
	}

	// ============================================================================================================
	//      Mesh surfaces 
	// ============================================================================================================

	FTimePoint MeshStartTime = FChrono::Now();
	MeshSurfaceByFront(QuadTrimmedSurfaceSet);
#ifdef CADKERNEL_DEV
	MesherReport.Chronos.GlobalMeshDuration = FChrono::Elapse(MeshStartTime);
	MesherReport.Chronos.GlobalDuration = FChrono::Elapse(StartTime);
#endif

}

void FParametricMesher::ApplyFaceCriteria(FTopologicalFace& Face, const TArray<TSharedPtr<FCriterion>>& Criteria, bool bThinZoneMeshing)
{
	if (Face.IsApplyCriteria())
	{
		return;
	}

	if (!Face.ComputeCriteriaGridSampling())
	{
		// The face is considered as degenerate, the face is delete, the process is canceled
		return;
	}

	FCriteriaGrid Grid(Face);

	Face.InitDeltaUs();
	Face.ApplyCriteria(Criteria, Grid);

	if (bThinZoneMeshing)
	{
		Grid.ScaleGrid();

#ifdef DEBUG_ONLY_SURFACE_TO_DEBUG
		if (Grid.bDisplay)
		{
			Grid.DisplayGridPoints(EGridSpace::Default2D);
			Grid.DisplayGridPoints(EGridSpace::UniformScaled);
			Grid.DisplayGridPoints(EGridSpace::Scaled);
		}
#endif
		FThinZone2DFinder ThinZoneFinder(Grid, Face);

		// Size (length of segment of the loop sampling) is equal to MinimalElementLength / ElementRatio
		// With this ratio each edges of the mesh should be defined by at least 3 segments. 
		// This should ensure to identified all thin zones according to the mesh size and minimizing the size of the loop sampling
		constexpr double ElementRatio = 3.;
		const double Size = Face.GetEstimatedMinimalElementLength() / ElementRatio;
		const bool bHasThinZones = ThinZoneFinder.SearchThinZones(Size);
		if (bHasThinZones)
		{
			Face.SetHasThinZone();
			Face.MoveThinZones(ThinZoneFinder.GetThinZones());
		}
	}

	if (Face.IsDegenerated())
	{
		Face.Remove();
	}
}

void FParametricMesher::ApplyEdgeCriteria(FTopologicalEdge& Edge)
{
	FTopologicalEdge& ActiveEdge = *Edge.GetLinkActiveEdge();
	ensureCADKernel(Edge.IsVirtuallyMeshed() || !ActiveEdge.IsApplyCriteria());

	Edge.ComputeCrossingPointCoordinates();
	Edge.InitDeltaUs();
	const TArray<double>& CrossingPointUs = Edge.GetCrossingPointUs();

	TArray<double> Coordinates;
	Coordinates.SetNum(CrossingPointUs.Num() * 2 - 1);
	Coordinates[0] = CrossingPointUs[0];
	for (int32 ICuttingPoint = 1; ICuttingPoint < Edge.GetCrossingPointUs().Num(); ICuttingPoint++)
	{
		Coordinates[2 * ICuttingPoint - 1] = (CrossingPointUs[ICuttingPoint - 1] + CrossingPointUs[ICuttingPoint]) * 0.5;
		Coordinates[2 * ICuttingPoint] = CrossingPointUs[ICuttingPoint];
	}

	TArray<FCurvePoint> Points3D;
	Edge.EvaluatePoints(Coordinates, 0, Points3D);

	const TArray<TSharedPtr<FCriterion>>& Criteria = GetMeshModel().GetCriteria();
	for (const TSharedPtr<FCriterion>& Criterion : Criteria)
	{
		Criterion->ApplyOnEdgeParameters(Edge, CrossingPointUs, Points3D);
	}

	Edge.SetApplyCriteriaMarker();
	ActiveEdge.SetApplyCriteriaMarker();
}

void FParametricMesher::Mesh(FTopologicalFace& Face)
{
#ifdef DEBUG_ONLY_SURFACE_TO_DEBUG
	bDisplay = (Face.GetId() == FaceToDebug);
#endif

	if (Face.IsNotMeshable())
	{
		return;
	}

	FMessage::Printf(EVerboseLevel::Debug, TEXT("Meshing of surface %d\n"), Face.GetId());

	FProgress _(1, TEXT("Meshing Entities : Mesh Surface"));

#ifdef DEBUG_CADKERNEL
	F3DDebugSession S(bDisplayDebugMeshStep, FString::Printf(TEXT("Mesh of surface %d"), Face.GetId()));
#endif

	FTimePoint StartTime = FChrono::Now();

	FTimePoint GenerateCloudStartTime = FChrono::Now();

	FGrid Grid(Face, MeshModel);
	if (!GenerateCloud(Grid) || Grid.IsDegenerated())
	{
#ifdef CADKERNEL_DEV
		MesherReport.Logs.AddDegeneratedGrid();
#endif
		FMessage::Printf(EVerboseLevel::Log, TEXT("The meshing of the surface %d failed due to a degenerated grid\n"), Face.GetId());
		Face.SetMeshed();
		return;
	}

	FDuration GenerateCloudDuration = FChrono::Elapse(GenerateCloudStartTime);

	FTimePoint IsoTriangulerStartTime = FChrono::Now();

	TSharedRef<FFaceMesh> SurfaceMesh = StaticCastSharedRef<FFaceMesh>(Face.GetOrCreateMesh(MeshModel));

	FIsoTriangulator IsoTrianguler(Grid, SurfaceMesh);
#ifdef CADKERNEL_DEV
	IsoTrianguler.SetMesherReport(MesherReport);
#endif

	if (IsoTrianguler.Triangulate())
	{
		if (Face.IsBackOriented())
		{
			SurfaceMesh->InverseOrientation();
		}
		MeshModel.AddMesh(SurfaceMesh);
	}
	Face.SetMeshed();

	FDuration TriangulateDuration = FChrono::Elapse(IsoTriangulerStartTime);
	FDuration Duration = FChrono::Elapse(StartTime);

#ifdef CADKERNEL_DEV
	MesherReport.Chronos.GlobalPointCloudDuration += Grid.Chronos.GeneratePointCloudDuration;
	MesherReport.Chronos.GlobalGeneratePointCloudDuration += GenerateCloudDuration;
	MesherReport.Chronos.GlobalTriangulateDuration += TriangulateDuration;
	MesherReport.Chronos.GlobalDelaunayDuration += IsoTrianguler.Chronos.FindSegmentToLinkLoopToLoopByDelaunayDuration;
	MesherReport.Chronos.GlobalMeshDuration += Duration;
#endif

	FChrono::PrintClockElapse(EVerboseLevel::Debug, TEXT("   "), TEXT("Meshing"), Duration);

}

bool FParametricMesher::GenerateCloud(FGrid& Grid)
{
	Grid.DefineCuttingParameters();
	if (!Grid.GeneratePointCloud())
	{
		return false;
	}

	if (bThinZoneMeshing)
	{
		FTimePoint StartTime = FChrono::Now();
		if (Grid.GetFace().HasThinZone())
		{
			MeshThinZoneEdges(Grid.GetFace());
		}
#ifdef CADKERNEL_DEV
		MesherReport.Chronos.GlobalThinZones += FChrono::Elapse(StartTime);
#endif
	}

	FTimePoint StartTime = FChrono::Now();
	MeshFaceLoops(Grid);

	Grid.ProcessPointCloud();

#ifdef CADKERNEL_DEV
	MesherReport.Chronos.GlobalMeshAndGetLoopNodes += FChrono::Elapse(StartTime);
#endif

	return true;
}

void FParametricMesher::MeshFaceLoops(FGrid& Grid)
{
	const FTopologicalFace& Face = Grid.GetFace();

	FTimePoint StartTime = FChrono::Now();

	for (const TSharedPtr<FTopologicalLoop>& Loop : Face.GetLoops())
	{
		for (const FOrientedEdge& Edge : Loop->GetEdges())
		{
			Mesh(*Edge.Entity, Face);
		}
	}

#ifdef CADKERNEL_DEV
	MesherReport.Chronos.GlobalMeshEdges += FChrono::Elapse(StartTime);
#endif
}

static void FillImposedIsoCuttingPoints(TArray<double>& UEdgeSetOfIntersectionWithIso, ECoordinateType CoordinateType, double EdgeToleranceGeo, const FTopologicalEdge& Edge, TArray<FCuttingPoint>& OutImposedIsoVertexSet)
{
	FLinearBoundary EdgeBoundary = Edge.GetBoundary();

	int32 StartIndex = OutImposedIsoVertexSet.Num();
	Algo::Sort(UEdgeSetOfIntersectionWithIso);
	double PreviousU = -HUGE_VALUE;
	for (double InterU : UEdgeSetOfIntersectionWithIso)
	{
		// Remove coordinate nearly equal to boundary
		if ((InterU - EdgeToleranceGeo) < EdgeBoundary.GetMin() || (InterU + EdgeToleranceGeo) > EdgeBoundary.GetMax())
		{
			continue;
		}

		// Remove coordinate inside thin zone
		for (FLinearBoundary ThinZone : Edge.GetThinZoneBounds())
		{
			if (ThinZone.Contains(InterU))
			{
				continue;
			}
		}

		// Remove nearly duplicate 
		if (InterU - PreviousU < EdgeToleranceGeo)
		{
			continue;
		}

		OutImposedIsoVertexSet.Emplace(InterU, CoordinateType);
		PreviousU = InterU;
	}

	int32 Index;
	int32 NewCoordinateCount = OutImposedIsoVertexSet.Num() - StartIndex;
	switch (NewCoordinateCount)
	{
	case 0:
		return;

	case 1:
	{
		int32 CuttingPointIndex = 0;
		while (CuttingPointIndex < Edge.GetCrossingPointUs().Num() && Edge.GetCrossingPointUs()[CuttingPointIndex] + DOUBLE_SMALL_NUMBER <= OutImposedIsoVertexSet[StartIndex].Coordinate)
		{
			++CuttingPointIndex;
		};
		if (CuttingPointIndex > 0)
		{
			--CuttingPointIndex;
		}
		OutImposedIsoVertexSet[StartIndex].IsoDeltaU = Edge.GetDeltaUMaxs()[CuttingPointIndex] * AQuarter;
		break;
	}

	default:
	{
		OutImposedIsoVertexSet[StartIndex].IsoDeltaU = (OutImposedIsoVertexSet[StartIndex + 1].Coordinate - OutImposedIsoVertexSet[StartIndex].Coordinate) * AQuarter;
		for (Index = StartIndex + 1; Index < OutImposedIsoVertexSet.Num() - 1; ++Index)
		{
			OutImposedIsoVertexSet[Index].IsoDeltaU = (OutImposedIsoVertexSet[Index + 1].Coordinate - OutImposedIsoVertexSet[Index - 1].Coordinate) * AEighth;
		}
		OutImposedIsoVertexSet[Index].IsoDeltaU = (OutImposedIsoVertexSet[Index].Coordinate - OutImposedIsoVertexSet[Index - 1].Coordinate) * AQuarter;
		break;
	}

	}
}

void FParametricMesher::Mesh(FTopologicalVertex& InVertex)
{
	InVertex.GetOrCreateMesh(GetMeshModel());
}

//#define DEBUG_INTERSECTEDGEISOS
#ifdef DEBUG_INTERSECTEDGEISOS
void DebugIntersectEdgeIsos(const FTopologicalFace& Face, const TArray<double>& IsoCoordinates, EIso TypeIso);
#endif

void FParametricMesher::Mesh(FTopologicalEdge& InEdge, const FTopologicalFace& Face)
{
	{
		FTopologicalEdge& ActiveEdge = *InEdge.GetLinkActiveEntity();
		if (ActiveEdge.IsMeshed())
		{
			if (ActiveEdge.GetMesh()->GetNodeCount() > 0)
			{
				return;
			}

			// In some case the 2d curve is a smooth curve and the 3d curve is a line and vice versa
			// In the particular case where the both case are opposed, we can have the 2d line sampled with 4 points, and the 2d curve sampled with 2 points (because in 3d, the 2d curve is a 3d line)
			// In this case, the loop is flat i.e. in 2d the meshes of the 2d line and 2d curve are coincident
			// So the grid is degenerated and the surface is not meshed
			// to avoid this case, the Edge is virtually meshed i.e. the nodes inside the edge have the id of the mesh of the vertices.
			InEdge.SetVirtuallyMeshedMarker();
		}

		if (ActiveEdge.IsThinPeak())
		{
			TArray<FCuttingPoint>& FinalEdgeCuttingPointCoordinates = ActiveEdge.GetCuttingPoints();
			FinalEdgeCuttingPointCoordinates.Emplace(ActiveEdge.GetStartCurvilinearCoordinates(), ECoordinateType::VertexCoordinate);
			FinalEdgeCuttingPointCoordinates.Emplace(ActiveEdge.GetEndCurvilinearCoordinates(), ECoordinateType::VertexCoordinate);
			GenerateEdgeElements(ActiveEdge);
			return;
		}
	}

	const FSurfacicTolerance& ToleranceIso = Face.GetIsoTolerances();

	// Get Edge intersection with inner surface mesh grid
	TArray<double> EdgeIntersectionWithIsoU_Coordinates;
	TArray<double> EdgeIntersectionWithIsoV_Coordinates;

	const TArray<double>& SurfaceTabU = Face.GetCuttingCoordinatesAlongIso(EIso::IsoU);
	const TArray<double>& SurfaceTabV = Face.GetCuttingCoordinatesAlongIso(EIso::IsoV);

	ApplyEdgeCriteria(InEdge);

#ifdef DEBUG_MESH_EDGE
	if (bDisplay)
	{
		F3DDebugSession _(FString::Printf(TEXT("EdgePointsOnDomain %d"), InEdge.GetId()));
		Display2D(InEdge);
		Wait();
	}
#endif

#ifdef DEBUG_INTERSECTEDGEISOS
	DebugIntersectEdgeIsos(Face, SurfaceTabU, EIso::IsoU);
	DebugIntersectEdgeIsos(Face, SurfaceTabV, EIso::IsoV);
	{
		F3DDebugSession _(FString::Printf(TEXT("Edge 2D %d"), InEdge.GetId()));
		UE::CADKernel::Display2D(InEdge);
	}
#endif

	InEdge.ComputeIntersectionsWithIsos(SurfaceTabU, EIso::IsoU, ToleranceIso, EdgeIntersectionWithIsoU_Coordinates);
	InEdge.ComputeIntersectionsWithIsos(SurfaceTabV, EIso::IsoV, ToleranceIso, EdgeIntersectionWithIsoV_Coordinates);

#ifdef DEBUG_INTERSECTEDGEISOS
	{
		F3DDebugSession _(FString::Printf(TEXT("Edge %d Intersect with iso"), InEdge.GetId()));
		TArray<FPoint2D> Intersections;
		InEdge.Approximate2DPoints(EdgeIntersectionWithIsoU_Coordinates, Intersections);
		for (const FPoint2D& Point : Intersections)
		{
			DisplayPoint(Point);
		}
		Intersections.Empty();
		InEdge.Approximate2DPoints(EdgeIntersectionWithIsoV_Coordinates, Intersections);
		for (const FPoint2D& Point : Intersections)
		{
			DisplayPoint(Point);
		}
		Wait();
	}
	{
		F3DDebugSession _(FString::Printf(TEXT("Thin Zone")));
		for (FLinearBoundary ThinZone : InEdge.GetThinZoneBounds())
		{
			TArray<double> Coords;
			Coords.Add(ThinZone.GetMin());
			Coords.Add(ThinZone.GetMax());
			TArray<FPoint2D> ThinZone2D;
			InEdge.Approximate2DPoints(Coords, ThinZone2D);
			DisplaySegment(ThinZone2D[0], ThinZone2D[1], EVisuProperty::YellowCurve);
		}
		Wait();

	}
#endif

	FLinearBoundary EdgeBounds = InEdge.GetBoundary();

	TArray<double>& DeltaUs = InEdge.GetDeltaUMaxs();

	InEdge.SortImposedCuttingPoints();
	const TArray<FImposedCuttingPoint>& EdgeImposedCuttingPoints = InEdge.GetImposedCuttingPoints();

	// build a edge mesh compiling inner surface cutting (based on criteria applied on the surface) and edge cutting (based on criteria applied on the curve)
	TArray<FCuttingPoint> ImposedIsoCuttingPoints;
	{
		int32 NbImposedCuttingPoints = EdgeImposedCuttingPoints.Num() + EdgeIntersectionWithIsoU_Coordinates.Num() + EdgeIntersectionWithIsoV_Coordinates.Num() + 2;
		ImposedIsoCuttingPoints.Reserve(NbImposedCuttingPoints);
	}

	FPoint2D ExtremityTolerances = InEdge.GetCurve()->GetExtremityTolerances(EdgeBounds);

	ImposedIsoCuttingPoints.Emplace(EdgeBounds.GetMin(), ECoordinateType::VertexCoordinate, -1, ExtremityTolerances[0]);
	ImposedIsoCuttingPoints.Emplace(EdgeBounds.GetMax(), ECoordinateType::VertexCoordinate, -1, ExtremityTolerances[1]);

	int32 Index = 0;
	for (const FImposedCuttingPoint& CuttingPoint : EdgeImposedCuttingPoints)
	{
		double CuttingPointDeltaU = InEdge.GetDeltaUFor(CuttingPoint.Coordinate, Index);
		ImposedIsoCuttingPoints.Emplace(CuttingPoint.Coordinate, ECoordinateType::ImposedCoordinate, CuttingPoint.OppositNodeIndex, CuttingPointDeltaU * AThird);
	}

	// Add Edge intersection with inner surface grid Iso
	double EdgeTolerance = FMath::Min(ExtremityTolerances[0], ExtremityTolerances[1]);
	if (!EdgeIntersectionWithIsoU_Coordinates.IsEmpty())
	{
		FillImposedIsoCuttingPoints(EdgeIntersectionWithIsoU_Coordinates, IsoUCoordinate, EdgeTolerance, InEdge, ImposedIsoCuttingPoints);
	}

	if (!EdgeIntersectionWithIsoV_Coordinates.IsEmpty())
	{
		FillImposedIsoCuttingPoints(EdgeIntersectionWithIsoV_Coordinates, IsoVCoordinate, EdgeTolerance, InEdge, ImposedIsoCuttingPoints);
	}

	ImposedIsoCuttingPoints.Sort([](const FCuttingPoint& Point1, const FCuttingPoint& Point2) { return Point1.Coordinate < Point2.Coordinate; });

	TFunction<void(int32&, int32&, ECoordinateType)> MergeImposedCuttingPoints = [&](int32& Index, int32& NewIndex, ECoordinateType NewType)
	{
		double DeltaU = FMath::Max(ImposedIsoCuttingPoints[NewIndex].IsoDeltaU, ImposedIsoCuttingPoints[Index].IsoDeltaU);
		if (ImposedIsoCuttingPoints[NewIndex].Type <= ImposedCoordinate && ImposedIsoCuttingPoints[Index].Type <= ImposedCoordinate)
		{
			DeltaU /= 5.;
		}

		if (ImposedIsoCuttingPoints[NewIndex].Coordinate + DeltaU > ImposedIsoCuttingPoints[Index].Coordinate)
		{
			if (ImposedIsoCuttingPoints[Index].Type == VertexCoordinate)
			{
				ImposedIsoCuttingPoints[NewIndex].Coordinate = ImposedIsoCuttingPoints[Index].Coordinate;
				ImposedIsoCuttingPoints[NewIndex].IsoDeltaU = ImposedIsoCuttingPoints[Index].IsoDeltaU;
			}
			else if (ImposedIsoCuttingPoints[NewIndex].Type == VertexCoordinate)
			{
			}
			else if (ImposedIsoCuttingPoints[NewIndex].Type == ImposedCoordinate)
			{
				if (ImposedIsoCuttingPoints[Index].Type == ImposedCoordinate)
				{
					ImposedIsoCuttingPoints[NewIndex].Coordinate = (ImposedIsoCuttingPoints[NewIndex].Coordinate + ImposedIsoCuttingPoints[Index].Coordinate) * 0.5;
				}
			}
			else if (ImposedIsoCuttingPoints[Index].Type == ImposedCoordinate)
			{
				ImposedIsoCuttingPoints[NewIndex].Coordinate = ImposedIsoCuttingPoints[Index].Coordinate;
				ImposedIsoCuttingPoints[NewIndex].Type = ImposedCoordinate;
				ImposedIsoCuttingPoints[NewIndex].IsoDeltaU = ImposedIsoCuttingPoints[Index].IsoDeltaU;
			}
			else if (ImposedIsoCuttingPoints[NewIndex].Type != ImposedIsoCuttingPoints[Index].Type)
			{
				ImposedIsoCuttingPoints[NewIndex].Coordinate = (ImposedIsoCuttingPoints[NewIndex].Coordinate + ImposedIsoCuttingPoints[Index].Coordinate) * 0.5;
				ImposedIsoCuttingPoints[NewIndex].Type = IsoUVCoordinate;
				ImposedIsoCuttingPoints[NewIndex].IsoDeltaU = FMath::Min(ImposedIsoCuttingPoints[NewIndex].IsoDeltaU, ImposedIsoCuttingPoints[Index].IsoDeltaU);
			}

			if (ImposedIsoCuttingPoints[NewIndex].Type <= ImposedCoordinate)
			{
				if (ImposedIsoCuttingPoints[NewIndex].OppositNodeIndex == -1)
				{
					ImposedIsoCuttingPoints[NewIndex].OppositNodeIndex = ImposedIsoCuttingPoints[Index].OppositNodeIndex;
				}
				else
				{
					ImposedIsoCuttingPoints[NewIndex].OppositNodeIndex2 = ImposedIsoCuttingPoints[Index].OppositNodeIndex;
				}
			}
		}
		else
		{
			++NewIndex;
			ImposedIsoCuttingPoints[NewIndex] = ImposedIsoCuttingPoints[Index];
		}
	};

	// If a pair of point isoU/isoV is too close, get the middle of the points
	if (ImposedIsoCuttingPoints.Num() > 1)
	{
		int32 NewIndex = 0;
		for (int32 Andex = 1; Andex < ImposedIsoCuttingPoints.Num(); ++Andex)
		{
			if (ImposedIsoCuttingPoints[Andex].Type > ECoordinateType::ImposedCoordinate)
			{
				bool bIsDelete = false;
				for (const FLinearBoundary& ThinZone : InEdge.GetThinZoneBounds())
				{
					if (ThinZone.Contains(ImposedIsoCuttingPoints[Andex].Coordinate))
					{
						bIsDelete = true;
						break; // or copntinue
					}
				}
				if (bIsDelete)
				{
					continue;
				}
			}

			if (ImposedIsoCuttingPoints[NewIndex].Type == ECoordinateType::ImposedCoordinate || ImposedIsoCuttingPoints[Andex].Type == ECoordinateType::ImposedCoordinate)
			{
				MergeImposedCuttingPoints(Andex, NewIndex, ECoordinateType::ImposedCoordinate);
			}
			else if (ImposedIsoCuttingPoints[NewIndex].Type != ImposedIsoCuttingPoints[Andex].Type)
			{
				MergeImposedCuttingPoints(Andex, NewIndex, ECoordinateType::IsoUVCoordinate);
			}
			else
			{
				++NewIndex;
				ImposedIsoCuttingPoints[NewIndex] = ImposedIsoCuttingPoints[Andex];
			}
		}
		ImposedIsoCuttingPoints.SetNum(NewIndex + 1);
	}

	if (ImposedIsoCuttingPoints.Num() > 1 && (EdgeBounds.GetMax() - ImposedIsoCuttingPoints.Last().Coordinate) < FMath::Min(ImposedIsoCuttingPoints.Last().IsoDeltaU, InEdge.GetDeltaUMaxs().Last()))
	{
		ImposedIsoCuttingPoints.Last().Coordinate = EdgeBounds.GetMax();
		ImposedIsoCuttingPoints.Last().Type = VertexCoordinate;
	}
	else
	{
		ImposedIsoCuttingPoints.Emplace(EdgeBounds.GetMax(), ECoordinateType::VertexCoordinate, -1, InEdge.GetDeltaUMaxs().Last() * AQuarter);
	}

	// Final array of the edge mesh vertex 
	TArray<FCuttingPoint>& FinalEdgeCuttingPointCoordinates = InEdge.GetCuttingPoints();
	{
		// max count of vertex
		double MinDeltaU = HUGE_VALUE;
		for (const double& DeltaU : DeltaUs)
		{
			if (DeltaU < MinDeltaU)
			{
				MinDeltaU = DeltaU;
			}
		}

		int32 MaxNumberOfVertex = FMath::IsNearlyZero(MinDeltaU) ? 5 : (int32)((EdgeBounds.GetMax() - EdgeBounds.GetMin()) / MinDeltaU) + 5;
		FinalEdgeCuttingPointCoordinates.Empty(ImposedIsoCuttingPoints.Num() + MaxNumberOfVertex);
	}

#ifdef DEBUG_GETPREFERREDUVCOORDINATESFROMNEIGHBOURS
	TArray<double> CuttingPoints2;
	{
		double ToleranceGeoEdge = FMath::Min(ExtremityTolerances[0], ExtremityTolerances[1]);

		TArray<FCuttingPoint> Extremities;
		Extremities.Reserve(2);
		Extremities.Emplace(EdgeBounds.GetMin(), ECoordinateType::VertexCoordinate, -1, ToleranceGeoEdge);
		Extremities.Emplace(EdgeBounds.GetMax(), ECoordinateType::VertexCoordinate, -1, ToleranceGeoEdge);

		FMesherTools::ComputeFinalCuttingPointsWithImposedCuttingPoints(InEdge.GetCrossingPointUs(), InEdge.GetDeltaUMaxs(), Extremities, CuttingPoints2);
	}
#endif

	if (InEdge.IsDegenerated() || InEdge.IsVirtuallyMeshed())
	{
		if (ImposedIsoCuttingPoints.Num() == 2)
		{
			ImposedIsoCuttingPoints.EmplaceAt(1, (ImposedIsoCuttingPoints[0].Coordinate + ImposedIsoCuttingPoints[1].Coordinate) * 0.5, ECoordinateType::OtherCoordinate);
		}

		for (FCuttingPoint CuttingPoint : ImposedIsoCuttingPoints)
		{
			FinalEdgeCuttingPointCoordinates.Emplace(CuttingPoint.Coordinate, ECoordinateType::OtherCoordinate);
		}
		InEdge.GetLinkActiveEdge()->SetMeshed();
	}
	else
	{
		TArray<double> CuttingPoints;
		FMesherTools::ComputeFinalCuttingPointsWithImposedCuttingPoints(InEdge.GetCrossingPointUs(), InEdge.GetDeltaUMaxs(), ImposedIsoCuttingPoints, CuttingPoints);
		int32 ImposedIndex = 0;
		int32 ImposedIsoCuttingPointsCount = ImposedIsoCuttingPoints.Num();
		for (const double& Coordinate : CuttingPoints)
		{
			if (FMath::IsNearlyEqual(ImposedIsoCuttingPoints[ImposedIndex].Coordinate, Coordinate))
			{
				FinalEdgeCuttingPointCoordinates.Emplace(ImposedIsoCuttingPoints[ImposedIndex]);
				++ImposedIndex;
			}
			else
			{
				while (ImposedIndex < ImposedIsoCuttingPointsCount && ImposedIsoCuttingPoints[ImposedIndex].Coordinate < Coordinate)
				{
					++ImposedIndex;
				}
				FinalEdgeCuttingPointCoordinates.Emplace(Coordinate, ECoordinateType::OtherCoordinate);
			}
		}

#ifdef DEBUG_GETPREFERREDUVCOORDINATESFROMNEIGHBOURS
		if (InEdge.IsThinZone() && EdgeImposedCuttingPoints.Num())
		{
			F3DDebugSession G(TEXT("Mesh(TSharedRef<FEdge> InEdge"));
			{
				F3DDebugSession G(TEXT("U From Iso"));
				for (const FCuttingPoint& CuttingU : ImposedIsoCuttingPoints)
				{
					if (CuttingU.OppositNodeIndex >= 0)
					{
						UE::CADKernel::DisplayPoint(FPoint2D(CuttingU.Coordinate, 0.0), EVisuProperty::RedPoint);
					}
					else
					{
						UE::CADKernel::DisplayPoint(FPoint2D(CuttingU.Coordinate, 0.0));
					}
				}
			}
			{
				F3DDebugSession _(InEdge.GetThinZoneBounds().Num() != 0, FString::Printf(TEXT("Thin Zone"), InEdge.GetId()));
				for (FLinearBoundary ThinZone : InEdge.GetThinZoneBounds())
				{
					UE::CADKernel::DisplayPoint(FPoint2D(ThinZone.GetMin(), 0.01), EVisuProperty::BluePoint);
					UE::CADKernel::DisplayPoint(FPoint2D(ThinZone.GetMax(), 0.01), EVisuProperty::BluePoint);
					DisplaySegment(FPoint2D(ThinZone.GetMin(), 0.01), FPoint2D(ThinZone.GetMax(), 0.01), EVisuProperty::BlueCurve);
				}
				Wait();
			}

			{
				F3DDebugSession G(TEXT("U From Criteria"));
				for (double CuttingU : CuttingPoints2)
				{
					UE::CADKernel::DisplayPoint(FPoint2D(CuttingU, 0.02), EVisuProperty::RedPoint);
				}
			}
			{
				F3DDebugSession G(TEXT("U Final (Criteria & Iso)"));
				for (double CuttingU : CuttingPoints)
				{
					UE::CADKernel::DisplayPoint(FPoint2D(CuttingU, 0.04), EVisuProperty::YellowPoint);
				}
			}
			Wait();
		}
#endif

		GenerateEdgeElements(InEdge);
	}

}

//#define DEBUG_MESH_EDGE
void FParametricMesher::GenerateEdgeElements(FTopologicalEdge& Edge)
{
	FTopologicalEdge& ActiveEdge = *Edge.GetLinkActiveEntity();

	bool bSameDirection = Edge.IsSameDirection(ActiveEdge);

	TSharedRef<FEdgeMesh> EdgeMesh = ActiveEdge.GetOrCreateMesh(MeshModel);

	int32 StartVertexNodeIndex = ActiveEdge.GetStartVertex()->GetOrCreateMesh(GetMeshModel())->GetMesh();
	int32 EndVertexNodeIndex = ActiveEdge.GetEndVertex()->GetOrCreateMesh(GetMeshModel())->GetMesh();

	TArray<double> CuttingPointCoordinates;
	CuttingPointCoordinates.Reserve(Edge.GetCuttingPoints().Num());
	for (const FCuttingPoint& CuttingPoint : Edge.GetCuttingPoints())
	{
		CuttingPointCoordinates.Add(CuttingPoint.Coordinate);
	}
	ensureCADKernel(CuttingPointCoordinates.Num() > 1);
	CuttingPointCoordinates.RemoveAt(0);
	CuttingPointCoordinates.Pop();

	TArray<FPoint>& Coordinates = EdgeMesh->GetNodeCoordinates();
	Edge.ApproximatePoints(CuttingPointCoordinates, Coordinates);

	if (!bSameDirection)
	{
		Algo::Reverse(Coordinates);
	}

#ifdef DEBUG_MESH_EDGE
	if (bDisplay)
	{
		F3DDebugSession _(FString::Printf(TEXT("Edge Mesh %d"), Edge.GetId()));
		TArray<FPoint2D> Mesh2D;
		Edge.Approximate2DPoints(CuttingPointCoordinates, Mesh2D);
		for (const FPoint2D& Vertex : Mesh2D)
		{
			DisplayPoint(Vertex, EVisuProperty::RedPoint);
		}
		Wait();
	}
#endif

	EdgeMesh->RegisterCoordinates();
	EdgeMesh->Mesh(StartVertexNodeIndex, EndVertexNodeIndex);
	MeshModel.AddMesh(EdgeMesh);
	ActiveEdge.SetMeshed();
}

void FParametricMesher::IsolateQuadFace(TArray<FCostToFace>& QuadSurfaces, TArray<FTopologicalFace*>& OtherSurfaces) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ParametricMesher::IsolateQuadFace);

	TArray<FTopologicalFace*> FlatQuadsAndTriangles;
	FlatQuadsAndTriangles.Reserve(Faces.Num());
	QuadSurfaces.Reserve(Faces.Num() * 2);
	OtherSurfaces.Reserve(Faces.Num());

	for (FTopologicalFace* FacePtr : Faces)
	{
		if (FacePtr == nullptr)
		{
			continue;
		}

		FTopologicalFace& Face = *FacePtr;
		Face.DefineSurfaceType();
		switch (Face.GetQuadType())
		{
		case EQuadType::Quadrangular:
			double LocalMinCurvature;
			double LocalMaxCurvature;
			GetMinMax(Face.GetCurvature(EIso::IsoU).Max, Face.GetCurvature(EIso::IsoV).Max, LocalMinCurvature, LocalMaxCurvature);
			if (LocalMaxCurvature > ConstMinCurvature)
			{
				QuadSurfaces.Emplace(LocalMaxCurvature, &Face);
				if (LocalMinCurvature > ConstMinCurvature)
				{
					QuadSurfaces.Emplace(LocalMinCurvature, &Face);
				}
			}
			else
			{
				FlatQuadsAndTriangles.Add(&Face);
				OtherSurfaces.Add(&Face);
			}
			break;
		case EQuadType::Triangular:
			FlatQuadsAndTriangles.Add(&Face);
			OtherSurfaces.Add(&Face);
			break;
		case EQuadType::Unset:
		default:
			OtherSurfaces.Add(&Face);
		}
	}

	Algo::Sort(QuadSurfaces, [](FCostToFace& SurfaceA, FCostToFace& SurfaceB)
		{
			return (SurfaceA.Cost > SurfaceB.Cost);
		}
	);

#ifdef DEBUG_ISOLATEQUADFACE
	if (QuadSurfaces.Num() > 0)
	{
		F3DDebugSession A(TEXT("Quad Entities"));
		for (const FCostToFace& Quad : QuadSurfaces)
		{
			F3DDebugSession _(FString::Printf(TEXT("Face %d %f"), Quad.Face->GetId(), Quad.Cost));
			Display(*Quad.Face);
		}
	}

	if (FlatQuadsAndTriangles.Num() > 0)
	{
		F3DDebugSession A(TEXT("Flat Quads & Triangles"));
		for (const FTopologicalFace* Face : FlatQuadsAndTriangles)
		{
			F3DDebugSession _(FString::Printf(TEXT("Face %d"), Face->GetId()));
			Display(*Face);
		}
	}

	if (OtherSurfaces.Num() > 0)
	{
		F3DDebugSession A(TEXT("Other Entities"));
		for (const FTopologicalFace* Face : OtherSurfaces)
		{
			F3DDebugSession _(FString::Printf(TEXT("Face %d"), Face->GetId()));
			Display(*Face);
		}
	}
	Wait();
#endif
}

void FParametricMesher::LinkQuadSurfaceForMesh(TArray<FCostToFace>& QuadTrimmedSurfaceSet, TArray<TArray<FTopologicalFace*>>& OutStrips)
{
	const double GeometricTolerance = 20. * MeshModel.GetGeometricTolerance();

	OutStrips.Reserve(QuadTrimmedSurfaceSet.Num());

	for (FCostToFace& Quad : QuadTrimmedSurfaceSet)
	{
		FTopologicalFace* Face = Quad.Face;
		const FSurfaceCurvature& Curvatures = Face->GetCurvatures();

		EIso Axe = (!RealCompare(Quad.Cost, Curvatures[EIso::IsoU].Max)) ? EIso::IsoU : EIso::IsoV;

		if (Axe == EIso::IsoU)
		{
			if (Face->HasMarker1())
			{
				continue;
			}
			Face->SetMarker1();
		}
		else
		{
			if (Face->HasMarker2())
			{
				continue;
			}
			Face->SetMarker2();
		}

		TArray<FTopologicalFace*>* QuadStrip = &OutStrips.Emplace_GetRef();
		QuadStrip->Reserve(QuadTrimmedSurfaceSet.Num());
		QuadStrip->Add(Face);

		const TArray<FEdge2DProperties>& SideProperties = Face->GetSideProperties();

		int32 StartSideIndex = 0;
		for (; StartSideIndex < 4; StartSideIndex++)
		{
			if (SideProperties[StartSideIndex].IsoType == Axe)
			{
				break;
			}
		}
		if (StartSideIndex == 4)
		{
			continue;
		}

		bool bFirstStep = true;
		int32 SideIndex = StartSideIndex;

		while (Face)
		{
			int32 EdgeIndex = Face->GetStartEdgeIndexOfSide(SideIndex);
			double SideLength = Face->GetSideProperties()[SideIndex].Length3D;
			TSharedPtr<FTopologicalEdge> Edge = Face->GetLoops()[0]->GetEdges()[EdgeIndex].Entity;

			Face = nullptr;
			FTopologicalEdge* NextEdge = Edge->GetFirstTwinEdge();
			if (NextEdge)
			{
				Face = NextEdge->GetLoop()->GetFace();
			}

			if (Face && (Face->GetQuadType() == EQuadType::Quadrangular || Face->GetQuadType() == EQuadType::Triangular))
			{
				// check side length
				int32 LocalEdgeIndex = Face->GetLoops()[0]->GetEdgeIndex(*NextEdge);
				SideIndex = Face->GetSideIndex(LocalEdgeIndex);
				double OtherSideLength = Face->GetSideProperties()[SideIndex].Length3D;

				GetMinMax(OtherSideLength, SideLength);
				if (SideLength - OtherSideLength > GeometricTolerance)
				{
					Face = nullptr;
				}
			}
			else
			{
				Face = nullptr;
			}

			if (Face)
			{
				// Set as processed in a direction
				const TArray<FEdge2DProperties>& LocalSideProperties = Face->GetSideProperties();
				if (LocalSideProperties[SideIndex].IsoType == EIso::IsoU)
				{
					if (Face->HasMarker1())
					{
						Face = nullptr;
					}
					else
					{
						Face->SetMarker1();
					}
				}
				else
				{
					if (Face->HasMarker2())
					{
						Face = nullptr;
					}
					else
					{
						Face->SetMarker2();
					}
				}
			}

			if (Face)
			{
				// it's a quad or a tri => add
				if (Face->GetQuadType() != EQuadType::Other)
				{
					QuadStrip->Add(Face);
				}

				if (Face->GetQuadType() == EQuadType::Triangular)
				{
					// stop
					Face = nullptr;
				}
			}

			if (!Face)
			{
				if (bFirstStep)
				{
					bFirstStep = false;
					Face = (*QuadStrip)[0];
					SideIndex = (StartSideIndex + 2) % 4;
					continue;
				}
				else
				{
					break;
				}
			}

			// find opposite side
			SideIndex = (SideIndex + 2) % 4;
		}

		if (QuadStrip->Num() == 1)
		{
			OutStrips.Pop();
		}
	}

	for (FTopologicalFace* Face : Faces)
	{
		if (Face == nullptr)
		{
			continue;
		}
		Face->ResetMarkers();
	}
}

void FParametricMesher::MeshSurfaceByFront(TArray<FCostToFace>& QuadTrimmedSurfaceSet)
{
	// Marker3 : Surfaces that have to be meshed are set Marker3
	// Marker1 : Surfaces added in CandidateSurfacesForMesh
	// Marker2 : Surfaces added in SecondChoiceOfCandidateSurfacesForMesh

	FMessage::Printf(EVerboseLevel::Debug, TEXT("Start MeshSurfaceByFront\n"));

	for (FTopologicalFace* Face : Faces)
	{
		if (Face == nullptr || Face->IsDeletedOrDegenerated())
		{
			continue;
		}
		Face->SetMarker3();
	}

	const double GeometricTolerance = 20. * MeshModel.GetGeometricTolerance();

	TArray<FTopologicalFace*> CandidateFacesForMesh; // first in first out
	CandidateFacesForMesh.Reserve(100);

	TArray<FTopologicalFace*> SecondChoiceOfCandidateFacesForMesh; // first in first out
	SecondChoiceOfCandidateFacesForMesh.Reserve(100);

	static bool bStop = false;

	TFunction<void(FTopologicalFace&)> MeshFace = [&](FTopologicalFace& Face)
	{
#ifdef DISPLAYDEBUGMESHFACEBYFACESTEP
		{
			F3DDebugSession A(FString::Printf(TEXT("Surface %d"), Face.GetId()));
			Display(Face);
		}
#endif
		Mesh(Face);

#ifdef DISPLAYDEBUGMESHFACEBYFACESTEP
		{
			F3DDebugSession A(FString::Printf(TEXT("Mesh %d"), Face.GetId()));
			DisplayMesh(*Face.GetOrCreateMesh(GetMeshModel()));
			Wait(bStop);
		}
#endif

		if (Face.HasMarker1())
		{
			CandidateFacesForMesh.RemoveSingle(&Face);
		}
		if (Face.HasMarker2())
		{
			SecondChoiceOfCandidateFacesForMesh.RemoveSingle(&Face);
		}

		const TSharedPtr<FTopologicalLoop>& Loop = Face.GetLoops()[0];
		for (const FOrientedEdge& OrientedEdge : Loop->GetEdges())
		{
			const FTopologicalEdge& Edge = *OrientedEdge.Entity;
			Edge.SetMarker1();

			for (FTopologicalEdge* NextEdge : Edge.GetTwinEntities())
			{
				if (NextEdge->HasMarker1())
				{
					continue;
				}

				FTopologicalFace* NextFace = NextEdge->GetFace();
				if ((NextFace == nullptr) || !NextFace->HasMarker3())
				{
					// not in the scope of surface to mesh
					continue;
				}

				int32 EdgeIndex;
				int32 LoopIndex;
				NextFace->GetEdgeIndex(*NextEdge, LoopIndex, EdgeIndex);
				if (LoopIndex > 0)
				{
					continue;
				}

				int32 SideIndex = NextFace->GetSideIndex(*NextEdge);
				if (SideIndex == -1)
				{
					// The face is not a quad
					continue;
				}

				FEdge2DProperties& SideProperty = NextFace->GetSideProperty(SideIndex);

				double EdgeLength = NextEdge->Length();
				SideProperty.MeshedLength += EdgeLength;
				NextFace->AddMeshedLength(EdgeLength);
				if ((SideProperty.Length3D - SideProperty.MeshedLength) < GeometricTolerance)
				{
					if (!SideProperty.bIsMesh)
					{
						SideProperty.bIsMesh = true;
						NextFace->MeshedSideNum()++;
					}

					if (!NextFace->HasMarker1())
					{
						NextFace->SetMarker1();
						CandidateFacesForMesh.Add(NextFace);
					}
				}
				else
				{
					if (!NextFace->HasMarker2())
					{
						NextFace->SetMarker2();
						SecondChoiceOfCandidateFacesForMesh.Add(NextFace);
					}
				}
			}
		}
	};

	TFunction<void(FTopologicalFace&)> MeshFacesByFront = [&](FTopologicalFace& Face)
	{
		if (Face.IsNotMeshable())
		{
			return;
		}

		MeshFace(Face);

		while (CandidateFacesForMesh.Num() || SecondChoiceOfCandidateFacesForMesh.Num())
		{
			// the candidate are sorted according to the number of meshed side 
			Algo::Sort(CandidateFacesForMesh, [](FTopologicalFace* Surface1, FTopologicalFace* Surface2) { return Surface1->MeshedSideNum() > Surface2->MeshedSideNum(); });

			int32 IndexOfBestCandidate = -1;
			double CandidateMeshedSideRatio = 0;

			// The first choice will be done in the first set of surface with the max meshed side numbers.
			if (CandidateFacesForMesh.Num())
			{
				int32 MaxMeshedSideNum = CandidateFacesForMesh[0]->MeshedSideNum();

				// next face with side well meshed are preferred
				int32 Index = 0;
				for (; Index < CandidateFacesForMesh.Num(); ++Index)
				{
					FTopologicalFace* CandidateSurface = CandidateFacesForMesh[Index];
					if (CandidateSurface->IsNotMeshable())
					{
						CandidateFacesForMesh.RemoveAt(Index);
						--Index;
						continue;
					}

					if (CandidateSurface->MeshedSideNum() < MaxMeshedSideNum)
					{
						break;
					}

					if (CandidateMeshedSideRatio < CandidateSurface->MeshedSideRatio())
					{
						CandidateMeshedSideRatio = CandidateSurface->MeshedSideRatio();
						IndexOfBestCandidate = Index;
					}
				}

				// if no candidate has been selected, the choice is done on all next surfaces
				if (IndexOfBestCandidate == -1)
				{
					for (; Index < CandidateFacesForMesh.Num(); ++Index)
					{
						FTopologicalFace* CandidateSurface = CandidateFacesForMesh[Index];
						if (CandidateSurface->IsNotMeshable())
						{
							CandidateFacesForMesh.RemoveAt(Index);
							--Index;
							continue;
						}

						if (CandidateMeshedSideRatio < CandidateSurface->MeshedSideRatio())
						{
							CandidateMeshedSideRatio = CandidateSurface->MeshedSideRatio();
							IndexOfBestCandidate = Index;
						}
					}
				}

				if (IndexOfBestCandidate >= 0)
				{
					MeshFace(*CandidateFacesForMesh[IndexOfBestCandidate]);
					continue;
				}
			}

			for (int32 Index = 0; Index < SecondChoiceOfCandidateFacesForMesh.Num(); ++Index)
			{
				FTopologicalFace* CandidateSurface = SecondChoiceOfCandidateFacesForMesh[Index];
				if (CandidateSurface->IsNotMeshable())
				{
					SecondChoiceOfCandidateFacesForMesh.RemoveAt(Index);
					--Index;
					continue;
				}

				if (CandidateMeshedSideRatio < CandidateSurface->MeshedSideRatio())
				{
					CandidateMeshedSideRatio = CandidateSurface->MeshedSideRatio();
					IndexOfBestCandidate = Index;
				}
			}

			if (IndexOfBestCandidate >= 0)
			{
				MeshFace(*SecondChoiceOfCandidateFacesForMesh[IndexOfBestCandidate]);
			}
		}
	};

	// the front is initialized with quad surface
	for (const FCostToFace& Quad : QuadTrimmedSurfaceSet)
	{
		FTopologicalFace* Face = Quad.Face;
		MeshFacesByFront(*Face);
	}

	// the the other surface
	for (FTopologicalFace* Face : Faces)
	{
		if (Face != nullptr && Face->IsMeshable())
		{
			MeshFacesByFront(*Face);
		}
	}

}

void FParametricMesher::MeshThinZoneEdges(FTopologicalFace& Face)
{
	TArray<FThinZone2D>& ThinZones = Face.GetThinZones();

	{
#ifdef DEBUG_MESHTHINSURF
		if(bDisplay)
		{
			F3DDebugSession _(bDisplay, FString::Printf(TEXT("Mesh Thin Face %d"), Face.GetId()));
		}
#endif

		for (FThinZone2D& Zone : ThinZones)
		{
			FindThinZoneBoundary(Zone.GetFirstSide());
			FindThinZoneBoundary(Zone.GetSecondSide());
		}

		for (FThinZone2D& Zone : ThinZones)
		{
			EMeshingState bFirstSideMeshingState = Zone.GetFirstSide().GetMeshingState();
			EMeshingState bSecondSideMeshingState = Zone.GetSecondSide().GetMeshingState();

			if (bFirstSideMeshingState == EMeshingState::FullyMeshed)
			{
				MeshThinZoneSide(Zone.GetFirstSide());
			}
			else if (bSecondSideMeshingState == EMeshingState::FullyMeshed)
			{
				MeshThinZoneSide(Zone.GetSecondSide());
			}
			else if (bFirstSideMeshingState == EMeshingState::PartiallyMeshed)
			{
				MeshThinZoneSide(Zone.GetFirstSide());
			}
			else if (bSecondSideMeshingState == EMeshingState::PartiallyMeshed)
			{
				MeshThinZoneSide(Zone.GetSecondSide());
			}
			else if (Zone.GetFirstSide().Length() > Zone.GetSecondSide().Length())
			{
				MeshThinZoneSide(Zone.GetFirstSide());
			}
			else
			{
				MeshThinZoneSide(Zone.GetSecondSide());
			}
		}
	}

	// if the extremity of the thin zone are connected by a short edges path, the edges path are not discretized to avoid a well discretized edge connecting to thin sides

}

void FParametricMesher::FindThinZoneBoundary(FThinZoneSide& Side)
{
	FTopologicalEdge* Edge = nullptr;
	FLinearBoundary SideEdgeCoordinate;

	for (FEdgeSegment& EdgeSegment : Side.GetSegments())
	{
		double UMin = EdgeSegment.GetCoordinate(ELimit::Start);
		double UMax = EdgeSegment.GetCoordinate(ELimit::End);
		GetMinMax(UMin, UMax);

		if (Edge != EdgeSegment.GetEdge())
		{
			if (Edge)
			{
				Edge->AddThinZone(SideEdgeCoordinate);
			}

			Edge = EdgeSegment.GetEdge();
			SideEdgeCoordinate.Set(UMin, UMax);
		}
		else
		{
			SideEdgeCoordinate.ExtendTo(UMin, UMax);
		}
	};
	Edge->AddThinZone(SideEdgeCoordinate);
}

void FParametricMesher::MeshThinZoneSide(FThinZoneSide& Side)
{
	typedef TFunction<bool(double, double)> CompareMethode;

	FTopologicalEdge* Edge = nullptr;

	int32 Index = 0;
	int32 Increment = 1;
	double UMin = 0.;
	double UMax = 0.;

	TArray<double> EdgeCuttingPointCoordinates;
	const TArray<int32>* NodeIndices = nullptr;

	TFunction<void(const FEdgeSegment&)> AddImposedCuttingPoint = [&](const FEdgeSegment& EdgeSegment)
	{
		for (; Index >= 0 && Index < EdgeCuttingPointCoordinates.Num(); Index += Increment)
		{
			if (EdgeCuttingPointCoordinates[Index] < UMin || EdgeCuttingPointCoordinates[Index] > UMax)
			{
				break;
			}

			FPoint2D CuttingPoint = EdgeSegment.ComputeEdgePoint(EdgeCuttingPointCoordinates[Index]);

			FEdgeSegment* ClosedSegment = EdgeSegment.GetCloseSegment();
			if (ClosedSegment == nullptr)
			{
#ifdef CADKERNEL_DEV
				ensureCADKernel(false);
				Wait();
#endif
				continue;
			}

			double OppositeCuttingPointSegmentU;
			FPoint2D OppositeCuttingPoint = ClosedSegment->ProjectPoint(CuttingPoint, OppositeCuttingPointSegmentU);

			double OppositeCuttingPointU = 0;
			FTopologicalEdge* OppositeEdge = nullptr;
			if (FMath::IsNearlyZero(OppositeCuttingPointSegmentU) && ClosedSegment->GetPrevious()->GetCloseSegment())
			{
				FEdgeSegment* PreviousClosedSegment = ClosedSegment->GetPrevious();
				OppositeCuttingPoint = PreviousClosedSegment->ProjectPoint(CuttingPoint, OppositeCuttingPointSegmentU);
				OppositeCuttingPointU = PreviousClosedSegment->ComputeEdgeCoordinate(OppositeCuttingPointSegmentU);
				OppositeEdge = PreviousClosedSegment->GetEdge();
			}
			else if (FMath::IsNearlyEqual(OppositeCuttingPointSegmentU, 1.) && ClosedSegment->GetNext()->GetCloseSegment())
			{
				FEdgeSegment* NextClosedSegment = ClosedSegment->GetNext();
				OppositeCuttingPoint = NextClosedSegment->ProjectPoint(CuttingPoint, OppositeCuttingPointSegmentU);
				OppositeCuttingPointU = NextClosedSegment->ComputeEdgeCoordinate(OppositeCuttingPointSegmentU);
				OppositeEdge = NextClosedSegment->GetEdge();
			}
			else
			{
				OppositeEdge = ClosedSegment->GetEdge();
				OppositeCuttingPointU = ClosedSegment->ComputeEdgeCoordinate(OppositeCuttingPointSegmentU);
			}

			if (OppositeEdge != Edge)
			{
				OppositeEdge->AddImposedCuttingPointU(OppositeCuttingPointU, (*NodeIndices)[Index]);
#ifdef DEBUG_MESHTHINSURF
				DisplayPoint2DWithScale(CuttingPoint, EVisuProperty::RedPoint);
				DisplaySegmentWithScale(ClosedSegment->GetExtemity(ELimit::End), ClosedSegment->GetExtemity(ELimit::Start));
				DisplayPoint2DWithScale(OppositeCuttingPoint);
				DisplaySegmentWithScale(CuttingPoint, OppositeCuttingPoint);
				Wait(false);
#endif
			}
		}
	};

	TFunction<void(double, CompareMethode)> FindFirstIndex = [&](double ULimit, CompareMethode Compare)
	{
		for (; Index >= 0 && Index < EdgeCuttingPointCoordinates.Num(); Index += Increment)
		{
			if (Compare(ULimit, EdgeCuttingPointCoordinates[Index]))
			{
				break;
			}
		}
	};

#ifdef DEBUG_MESHTHINSURF
	Open3DDebugSession(TEXT("MeshThinZoneSide"));
#endif

	for (FEdgeSegment& EdgeSegment : Side.GetSegments())
	{
		UMin = EdgeSegment.GetCoordinate(ELimit::Start);
		UMax = EdgeSegment.GetCoordinate(ELimit::End);
		GetMinMax(UMin, UMax);

		if (Edge != EdgeSegment.GetEdge())
		{
#ifdef DEBUG_MESHTHINSURF
			if (Edge)
			{
				Close3DDebugSession();
			}
			Open3DDebugSession(TEXT("Projection of mesh"));
#endif
			Edge = EdgeSegment.GetEdge();
			if (Edge == nullptr)
			{
				continue;
			}

			if (!Edge->IsMeshed())
			{
				Mesh(*Edge, *Edge->GetFace());
			}

			FEdgeMesh& EdgeMesh = *Edge->GetOrCreateMesh(MeshModel);
			NodeIndices = &EdgeMesh.EdgeVerticesIndex;
			GetCuttingPointCoordinates(Edge->GetCuttingPoints(), EdgeCuttingPointCoordinates);


			if (EdgeCuttingPointCoordinates.Num() == 0)
			{
				TArray<FPoint>& NodeCoordinates = EdgeMesh.GetNodeCoordinates();
				TArray<FPoint> ProjectedPoints;
				Edge->ProjectPoints(NodeCoordinates, EdgeCuttingPointCoordinates, ProjectedPoints);
				if (EdgeCuttingPointCoordinates.Num() > 1 && EdgeCuttingPointCoordinates[0] > EdgeCuttingPointCoordinates[1])
				{
					Algo::Reverse(EdgeCuttingPointCoordinates);
				}
				EdgeCuttingPointCoordinates.EmplaceAt(0, Edge->GetStartCurvilinearCoordinates());
				EdgeCuttingPointCoordinates.Emplace(Edge->GetEndCurvilinearCoordinates());
			}

			ensureCADKernel(EdgeCuttingPointCoordinates[0] < EdgeCuttingPointCoordinates[1]);

			bool bEdgeIsForward = EdgeSegment.IsForward();
			if (bEdgeIsForward)
			{
				Index = 0;
				Increment = 1;
				FindFirstIndex(UMin, [](double Value1, double Value2) {return (Value1 < Value2); });
			}
			else
			{
				Index = (int32)EdgeCuttingPointCoordinates.Num() - 1;
				Increment = -1;
				FindFirstIndex(UMax, [](double Value1, double Value2) {return (Value1 > Value2); });
			}
		}

		AddImposedCuttingPoint(EdgeSegment);
	}

#ifdef DEBUG_MESHTHINSURF
	Close3DDebugSession();
	Close3DDebugSession();
	//Wait();
#endif

}

#ifdef DEBUG_INTERSECTEDGEISOS
TMap<int32, int32> SurfaceDrawed;
bool bDisplayIsoCurve = true;

void DebugIntersectEdgeIsos(const FTopologicalFace& Face, const TArray<double>& IsoCoordinates, EIso TypeIso)
{
	if (SurfaceDrawed.Find(Face.GetId()) == nullptr)
	{
		SurfaceDrawed.Add(Face.GetId(), 0);
	}

	if (bDisplayIsoCurve && SurfaceDrawed[Face.GetId()] < 2)
	{
		SurfaceDrawed[Face.GetId()]++;

		FSurfacicBoundary Bounds = Face.GetBoundary();

		//{
		//	F3DDebugSession _(FString::Printf(TEXT("Iso %s 3D %d"), TypeIso == EIso::IsoU ? TEXT("U") : TEXT("V"), Face.GetId()));
		//	for (double U : IsoCoordinates)
		//	{
		//		DisplayIsoCurve(*Face.GetCarrierSurface(), U, TypeIso);
		//	}
		//}

		F3DDebugSession _(FString::Printf(TEXT("Iso %s 2D %d"), TypeIso == EIso::IsoU ? TEXT("U") : TEXT("V"), Face.GetId()));
		if (TypeIso == EIso::IsoU)
		{
			for (double U : IsoCoordinates)
			{
				FPoint2D Start(U, Bounds[EIso::IsoV].Min);
				FPoint2D End(U, Bounds[EIso::IsoV].Max);
				DisplaySegment(Start, End, 0, EVisuProperty::Iso);
			}
		}
		else
		{
			for (double V : IsoCoordinates)
			{
				FPoint2D Start(Bounds[EIso::IsoU].Min, V);
				FPoint2D End(Bounds[EIso::IsoU].Max, V);
				DisplaySegment(Start, End, 0, EVisuProperty::Iso);
			}
		}
	}
}

#endif

} // namespace