// Copyright Epic Games, Inc. All Rights Reserved.

#include "CuttingOps/EmbedPolygonsOp.h"

#include "DynamicMeshEditor.h"
#include "Selections/MeshFaceSelection.h"
#include "MeshQueries.h"
#include "Operations/EmbedSurfacePath.h"
#include "Operations/SimpleHoleFiller.h"

#include "Engine/StaticMesh.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "MeshSimplification.h"
#include "MeshConstraintsUtil.h"
#include "Operations/MeshPlaneCut.h"
#include "ConstrainedDelaunay2.h"


void CollapseDegenerateEdgesOnVertexPath(FDynamicMesh3& Mesh, TArray<int>& VertexIDsIO, TArray<int>& PathVertCorrespondIO)
{
	// similar to the CollapseDegenerateEdges in FMeshPlaneCut, but tailored to this use case
	// maintains the vertex ID correspondence to original verts across the update
	TArray<int> VertexIDs = VertexIDsIO; // copy of inputs IDs
	TMultiMap<int, int> VertexIDToPathVertIdx;
	for (int PathIdx = 0; PathIdx < PathVertCorrespondIO.Num(); PathIdx++)
	{
		VertexIDToPathVertIdx.Add(VertexIDs[PathVertCorrespondIO[PathIdx]], PathIdx);
	}

	// build edge set directly rather than use FEdgeLoop because (1) we want a set, (2) we want to forgive edges not being there (not check() on that case)
	TSet<int> Edges;
	for (int LastIdx = VertexIDs.Num() - 1, Idx = 0; Idx < VertexIDs.Num(); LastIdx = Idx++)
	{
		int EID = Mesh.FindEdge(VertexIDs[LastIdx], VertexIDs[Idx]);
		if (EID >= 0)
		{
			Edges.Add(EID);
		}
	}

	const double DegenerateEdgeTol = .1;
	double Tol2 = DegenerateEdgeTol * DegenerateEdgeTol;
	FVector3d A, B;
	int Collapsed = 0;
	TArray<int> FoundValues;
	do
	{
		Collapsed = 0;
		for (int EID : Edges)
		{
			if (!Mesh.IsEdge(EID))
			{
				continue;
			}
			Mesh.GetEdgeV(EID, A, B);
			double DSq = A.DistanceSquared(B);
			if (DSq > Tol2)
			{
				continue;
			}

			FIndex2i EV = Mesh.GetEdgeV(EID);
			// if the vertex we'd remove is on a seam, try removing the other one instead
			if (Mesh.HasAttributes() && Mesh.Attributes()->IsSeamVertex(EV.B, false))
			{
				Swap(EV.A, EV.B);
				// if they were both on seams, then collapse should not happen?  (& would break OnCollapseEdge assumptions in overlay)
				if (Mesh.HasAttributes() && Mesh.Attributes()->IsSeamVertex(EV.B, false))
				{
					continue;
				}
			}
			FDynamicMesh3::FEdgeCollapseInfo CollapseInfo;
			EMeshResult Result = Mesh.CollapseEdge(EV.A, EV.B, CollapseInfo);
			if (Result == EMeshResult::Ok)
			{
				// move everything mapped on the removed vertex over to the kept vertex
				if (VertexIDToPathVertIdx.Contains(CollapseInfo.RemovedVertex))
				{
					FoundValues.Reset();
					VertexIDToPathVertIdx.MultiFind(CollapseInfo.RemovedVertex, FoundValues);
					for (int V : FoundValues)
					{
						VertexIDToPathVertIdx.Add(CollapseInfo.KeptVertex, V);
					}
					VertexIDToPathVertIdx.Remove(CollapseInfo.RemovedVertex);
				}
				Collapsed++;
			}
		}
	}
	while (Collapsed > 0);

	// update vertex ids array and correspondence from input path indices to vertex ids array indices
	VertexIDsIO.Reset();
	for (int VID : VertexIDs)
	{
		if (Mesh.IsVertex(VID))
		{
			int NewIdx = VertexIDsIO.Num();
			VertexIDsIO.Add(VID);
			FoundValues.Reset();
			VertexIDToPathVertIdx.MultiFind(VID, FoundValues);
			for (int V : FoundValues)
			{
				PathVertCorrespondIO[V] = NewIdx;
			}
		}
	}
}

void FEmbedPolygonsOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress->Cancelled())
	{
		return;
	}
	ResultMesh->Copy(*OriginalMesh, true, true, true, !bDiscardAttributes);

	double MeshRadius = OriginalMesh->GetBounds().MaxDim();
	double UVScaleFactor = 1.0 / MeshRadius;

	bool bCollapseDegenerateEdges = true; // TODO make this optional?
	
	FFrame3d Frame = PolygonFrame;
	Frame.Origin = Frame.Origin + (2*MeshRadius*Frame.Z());

	FPolygon2d Polygon = GetPolygon();
	double Perimeter = Polygon.Perimeter();

	TArray<int> PathVertIDs1, PathVertCorrespond1;
	
	TArray<TPair<float, int>> SortedHitTriangles;
	TMeshQueries<FDynamicMesh3>::FindHitTriangles_LinearSearch(*ResultMesh, FRay3d(Frame.FromPlaneUV(Polygon[0]), -Frame.Z()), SortedHitTriangles);

	if (SortedHitTriangles.Num() < 1)
	{
		// didn't hit the mesh 
		return;
	}

	int SecondHit = 1;
	if (Operation == EEmbeddedPolygonOpMethod::CutThrough)
	{
		while (SecondHit < SortedHitTriangles.Num() && FMath::IsNearlyEqual(SortedHitTriangles[SecondHit].Key, SortedHitTriangles[0].Key))
		{
			SecondHit++;
		}
		if (SecondHit >= SortedHitTriangles.Num())
		{
			// failed to find a second surface to connect to
			return;
		}
	}

	auto CutHole = [](FDynamicMesh3& Mesh, const FFrame3d& F, int TriStart, const FPolygon2d& PolygonArg, TArray<int>& PathVertIDs, TArray<int>& PathVertCorrespond, bool bCollapseDegenerateEdges)
	{
		if (!Mesh.IsTriangle(TriStart))
		{
			return false;
		}
		FMeshFaceSelection Selection(&Mesh);
		bool bDidEmbed = EmbedProjectedPath(&Mesh, TriStart, F, PolygonArg.GetVertices(), PathVertIDs, PathVertCorrespond, true, &Selection);
		if (!bDidEmbed)
		{
			return false;
		}

		FDynamicMeshEditor MeshEditor(&Mesh);
		bool bDidRemove = MeshEditor.RemoveTriangles(Selection.AsArray(), true);
		if (!bDidRemove)
		{
			return false;
		}

		// remove triangles could have removed a path vertex entirely in weird cases; just consider that a failure
		for (int VID : PathVertIDs)
		{
			if (!Mesh.IsVertex(VID))
			{
				return false;
			}
		}

		// collapse degenerate edges if we got em
		if (bCollapseDegenerateEdges)
		{
			CollapseDegenerateEdgesOnVertexPath(Mesh, PathVertIDs, PathVertCorrespond);
		}

		// For hole cut to be counted as success, hole cut vertices must be valid, unique, and correspond to valid boundary edges
		TSet<int> SeenVIDs;
		for (int LastIdx = PathVertIDs.Num() - 1, Idx = 0; Idx < PathVertIDs.Num(); LastIdx=Idx++)
		{
			check(Mesh.IsVertex(PathVertIDs[Idx])); // collapse shouldn't leave invalid verts in, and we check + fail out on invalid verts above that, so seeing them here should be impossible
			int EID = Mesh.FindEdge(PathVertIDs[LastIdx], PathVertIDs[Idx]);
			if (!Mesh.IsEdge(EID) || !Mesh.IsBoundaryEdge(EID))
			{
				return false;
			}
			if (SeenVIDs.Contains(PathVertIDs[Idx]))
			{
				return false;
			}
			SeenVIDs.Add(PathVertIDs[Idx]);
		}

		return true;
	};

	bool bCutSide1 = CutHole(*ResultMesh, Frame, SortedHitTriangles[0].Value, Polygon, PathVertIDs1, PathVertCorrespond1, bCollapseDegenerateEdges);
	RecordEmbeddedEdges(PathVertIDs1);
	if (!bCutSide1 || PathVertIDs1.Num() < 2)
	{
		return;
	}

	if (Operation == EEmbeddedPolygonOpMethod::CutThrough)
	{
		TArray<int> PathVertIDs2, PathVertCorrespond2;
		bool bCutSide2 = CutHole(*ResultMesh, Frame, SortedHitTriangles[SecondHit].Value, Polygon, PathVertIDs2, PathVertCorrespond2, bCollapseDegenerateEdges);
		RecordEmbeddedEdges(PathVertIDs1);
		if (!bCutSide2 || PathVertIDs2.Num() < 2)
		{
			return;
		}
		FDynamicMeshEditor MeshEditor(ResultMesh.Get());
		FDynamicMeshEditResult ResultOut;
		bool bStitched = MeshEditor.StitchSparselyCorrespondedVertexLoops(PathVertIDs1, PathVertCorrespond1, PathVertIDs2, PathVertCorrespond2, ResultOut);
		if (bStitched && ResultMesh->HasAttributes())
		{
			MeshEditor.SetTubeNormals(ResultOut.NewTriangles, PathVertIDs1, PathVertCorrespond1, PathVertIDs2, PathVertCorrespond2);
			TArray<float> UValues; UValues.SetNumUninitialized(PathVertCorrespond2.Num() + 1);
			FVector3f ZVec = -(FVector3f)Frame.Z();
			float Along = 0;
			for (int UIdx = 0; UIdx < UValues.Num(); UIdx++)
			{
				UValues[UIdx] = Along;
				Along += Polygon[UIdx % Polygon.VertexCount()].Distance(Polygon[(UIdx + 1) % Polygon.VertexCount()]);
			}

			for (int UVIdx = 0, NumUVLayers = ResultMesh->Attributes()->NumUVLayers(); UVIdx < NumUVLayers; UVIdx++)
			{
				MeshEditor.SetGeneralTubeUVs(ResultOut.NewTriangles,
					PathVertIDs2, PathVertCorrespond2, PathVertIDs1, PathVertCorrespond1,
					UValues, ZVec,
					UVScaleFactor, FVector2f::Zero(), UVIdx
				);
			}
		}
	}
	else if (Operation == EEmbeddedPolygonOpMethod::CutAndFill)
	{
		FDynamicMeshEditor MeshEditor(ResultMesh.Get());
		FEdgeLoop Loop(ResultMesh.Get());
		Loop.InitializeFromVertices(PathVertIDs1);

		
		FSimpleHoleFiller Filler(ResultMesh.Get(), Loop);
		int GID = ResultMesh->AllocateTriangleGroup();
		if (Filler.Fill(GID))
		{
			if (ResultMesh->HasAttributes())
			{
				MeshEditor.SetTriangleNormals(Filler.NewTriangles, (FVector3f)Frame.Z()); // TODO: should we use the best fit plane instead of the projection plane for the normal? ... probably.
				
				for (int UVIdx = 0, NumUVLayers = ResultMesh->Attributes()->NumUVLayers(); UVIdx < NumUVLayers; UVIdx++)
				{
					MeshEditor.SetTriangleUVsFromProjection(Filler.NewTriangles, Frame, UVScaleFactor, FVector2f::Zero(), true, UVIdx);
				}
			}
		}
	}

	bEmbedSucceeded = true;
}

void FEmbedPolygonsOp::RecordEmbeddedEdges(TArray<int>& PathVertIDs)
{
	for (int Idx = 0; Idx + 1 < PathVertIDs.Num(); Idx++)
	{
		int EID = ResultMesh->FindEdge(PathVertIDs[Idx], PathVertIDs[Idx + 1]);
		if (ensure(ResultMesh->IsEdge(EID)))
		{
			EmbeddedEdges.Add(EID);
		}
	}
}