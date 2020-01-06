// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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


void FEmbedPolygonsOp::SetTransform(const FTransform& Transform) {
	ResultTransform = (FTransform3d)Transform;
}

void FEmbedPolygonsOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress->Cancelled())
	{
		return;
	}
	ResultMesh->Copy(*OriginalMesh, true, true, true, !bDiscardAttributes);



	
	FVector3d p = LocalPlaneOrigin + LocalPlaneNormal * 1000;
	FVector3d d = -LocalPlaneNormal;
	FFrame3d Frame(p, d);
	

	FPolygon2d Polygon = GetPolygon();
	
	TArray<int> PathVertIDs1, PathVertCorrespond1;
	
	TArray<TPair<float, int>> SortedHitTriangles;
	TMeshQueries<FDynamicMesh3>::FindHitTriangles_LinearSearch(*ResultMesh, FRay3d(Frame.FromPlaneUV(Polygon[0]), Frame.Z()), SortedHitTriangles);

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

	auto CutHole = [](FDynamicMesh3& Mesh, const FFrame3d& F, int TriStart, const FPolygon2d& PolygonArg, TArray<int>& PathVertIDs, TArray<int>& PathVertCorrespond)
	{
		FMeshFaceSelection Selection(&Mesh);
		bool bDidEmbed = EmbedProjectedPath(&Mesh, TriStart, F, PolygonArg.GetVertices(), PathVertIDs, PathVertCorrespond, true, &Selection);
		if (!bDidEmbed)
		{
			return false;
		}
		// slow debugging check, TODO: remove
		for (int VID : PathVertIDs)
		{
			ensure(Mesh.IsVertex(VID));
		}

		FDynamicMeshEditor MeshEditor(&Mesh);
		bool bDidRemove = MeshEditor.RemoveTriangles(Selection.AsArray(), true);
		if (!bDidRemove)
		{
			return false;
		}
		for (int VID : PathVertIDs)
		{
			// check if the triangle removal nuked the vertices on the boundary; TODO: don't 'ensure' if we need to keep this, but probably some earlier code should handle this ...
			if (!ensure(Mesh.IsVertex(VID)))
			{
				return false;
			}
		}

		return true;
	};

	bool bCutSide1 = CutHole(*ResultMesh, Frame, SortedHitTriangles[0].Value, Polygon, PathVertIDs1, PathVertCorrespond1);
	if (!bCutSide1 || PathVertIDs1.Num() < 2)
	{
		return;
	}

	if (Operation == EEmbeddedPolygonOpMethod::CutThrough)
	{
		TArray<int> PathVertIDs2, PathVertCorrespond2;
		bool bCutSide2 = CutHole(*ResultMesh, Frame, SortedHitTriangles[SecondHit].Value, Polygon, PathVertIDs2, PathVertCorrespond2);
		if (!bCutSide2 || PathVertIDs2.Num() < 2)
		{
			return;
		}
		FDynamicMeshEditor MeshEditor(ResultMesh.Get());
		FDynamicMeshEditResult ResultOut;
		bool bStitched = MeshEditor.StitchSparselyCorrespondedVertexLoops(PathVertIDs2, PathVertCorrespond2, PathVertIDs1, PathVertCorrespond1, ResultOut);
		// TODO: set triangle normals and uvs if mesh hasattributes
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
				MeshEditor.SetTriangleNormals(Filler.NewTriangles, (FVector3f)-Frame.Z()); // TODO: should we use the best fit plane instead of the projection plane for the normal? ... probably.
				float UVScaleFactor = 1; // TODO: this should be a parameter
				MeshEditor.SetTriangleUVsFromProjection(Filler.NewTriangles, Frame, UVScaleFactor);
			}
		}
	}
}