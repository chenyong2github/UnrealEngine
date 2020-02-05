// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/InsetMeshRegion.h"
#include "MeshNormals.h"
#include "DynamicMeshEditor.h"
#include "Selections/MeshVertexSelection.h"
#include "DynamicMeshChangeTracker.h"
#include "Selections/MeshConnectedComponents.h"
#include "Distance/DistLine3Line3.h"


FInsetMeshRegion::FInsetMeshRegion(FDynamicMesh3* mesh) : Mesh(mesh)
{
}


bool FInsetMeshRegion::Apply()
{
	FMeshNormals Normals;
	bool bHaveVertexNormals = Mesh->HasVertexNormals();
	if (!bHaveVertexNormals)
	{
		Normals = FMeshNormals(Mesh);
		Normals.ComputeVertexNormals();
	}

	FMeshConnectedComponents RegionComponents(Mesh);
	RegionComponents.FindConnectedTriangles(Triangles);

	bool bAllOK = true;
	InsetRegions.SetNum(RegionComponents.Num());
	for (int32 k = 0; k < RegionComponents.Num(); ++k)
	{
		FInsetInfo& Region = InsetRegions[k];
		Region.InitialTriangles = MoveTemp(RegionComponents.Components[k].Indices);
		if (ApplyInset(Region, (bHaveVertexNormals) ? nullptr : &Normals) == false)
		{
			bAllOK = false;
		}
		else
		{
			AllModifiedTriangles.Append(Region.InitialTriangles);
			for (TArray<int32>& RegionTris : Region.StitchTriangles)
			{
				AllModifiedTriangles.Append(RegionTris);
			}
		}
	}

	return bAllOK;


}


bool FInsetMeshRegion::ApplyInset(FInsetInfo& Region, FMeshNormals* UseNormals)
{
	FMeshRegionBoundaryLoops InitialLoops(Mesh, Region.InitialTriangles, false);
	bool bOK = InitialLoops.Compute();
	if (bOK == false)
	{
		return false;
	}

	int32 NumInitialLoops = InitialLoops.GetLoopCount();

	if (ChangeTracker)
	{
		ChangeTracker->SaveTriangles(Region.InitialTriangles, true);
	}

	FDynamicMeshEditor Editor(Mesh);

	TArray<FDynamicMeshEditor::FLoopPairSet> LoopPairs;
	bOK = Editor.DisconnectTriangles(Region.InitialTriangles, LoopPairs, true);
	if (bOK == false)
	{
		return false;
	}

	// inset vertices
	for (FDynamicMeshEditor::FLoopPairSet& LoopPair : LoopPairs)
	{
		int32 NumEdges = LoopPair.InnerEdges.Num();
		TArray<FLine3d> InsetLines;
		InsetLines.SetNum(NumEdges);

		for (int32 k = 0; k < NumEdges; ++k)
		{
			FIndex4i EdgeVT = Mesh->GetEdge(LoopPair.InnerEdges[k]);
			FVector3d A = Mesh->GetVertex(EdgeVT.A);
			FVector3d B = Mesh->GetVertex(EdgeVT.B);
			FVector3d EdgeDir = (A - B).Normalized();
			FVector3d Midpoint = (A + B) * 0.5;
			int32 EdgeTri = EdgeVT.C;
			FVector3d Normal, Centroid; double Area;
			Mesh->GetTriInfo(EdgeTri, Normal, Area, Centroid);
			
			FVector3d InsetDir = Normal.Cross(EdgeDir);
			if ((Centroid - Midpoint).Dot(InsetDir) < 0)
			{
				InsetDir = -InsetDir;
			}

			InsetLines[k] = FLine3d(Midpoint + InsetDistance * InsetDir, EdgeDir);
		}

		int32 NumVertices = LoopPair.InnerVertices.Num();
		for (int32 vi = 0; vi < NumVertices; ++vi)
		{
			const FLine3d& PrevLine = InsetLines[(vi == 0) ? (NumEdges-1) : (vi-1)];
			const FLine3d& NextLine = InsetLines[vi];

			FDistLine3Line3d Distance(PrevLine, NextLine);
			double DistSqr = Distance.GetSquared();

			FVector3d CurPos = Mesh->GetVertex(LoopPair.InnerVertices[vi]);
			FVector3d NewPos = 0.5 * (Distance.Line1ClosestPoint + Distance.Line2ClosestPoint);
			Mesh->SetVertex(LoopPair.InnerVertices[vi], NewPos);
		}

	};

	// stitch each loop
	Region.BaseLoops.SetNum(NumInitialLoops);
	Region.InsetLoops.SetNum(NumInitialLoops);
	Region.StitchTriangles.SetNum(NumInitialLoops);
	Region.StitchPolygonIDs.SetNum(NumInitialLoops);
	int32 LoopIndex = 0;
	for (FDynamicMeshEditor::FLoopPairSet& LoopPair : LoopPairs)
	{
		TArray<int32>& BaseLoopV = LoopPair.OuterVertices;
		TArray<int32>& InsetLoopV = LoopPair.InnerVertices;
		int32 NumLoopV = BaseLoopV.Num();

		// allocate a new group ID for each pair of input group IDs, and build up list of new group IDs along loop
		TArray<int32> NewGroupIDs;
		TArray<int32> EdgeGroups;
		TMap<TPair<int32, int32>, int32> NewGroupsMap;
		for (int32 k = 0; k < NumLoopV; ++k)
		{
			int32 InsetEdgeID = Mesh->FindEdge(InsetLoopV[k], InsetLoopV[(k + 1) % NumLoopV]);
			int32 InsetGroupID = Mesh->GetTriangleGroup(Mesh->GetEdgeT(InsetEdgeID).A);

			// base edge may not exist if we inset entire region. In that case just use single GroupID
			int32 BaseEdgeID = Mesh->FindEdge(BaseLoopV[k], BaseLoopV[(k + 1) % NumLoopV]);
			int32 BaseGroupID = (BaseEdgeID >= 0) ? Mesh->GetTriangleGroup(Mesh->GetEdgeT(BaseEdgeID).A) : InsetGroupID;

			TPair<int32, int32> GroupPair(FMathd::Min(BaseGroupID, InsetGroupID), FMathd::Max(BaseGroupID, InsetGroupID));
			if (NewGroupsMap.Contains(GroupPair) == false)
			{
				int32 NewGroupID = Mesh->AllocateTriangleGroup();
				NewGroupIDs.Add(NewGroupID);
				NewGroupsMap.Add(GroupPair, NewGroupID);
			}
			EdgeGroups.Add(NewGroupsMap[GroupPair]);
		}

		// stitch the loops
		FDynamicMeshEditResult StitchResult;
		Editor.StitchVertexLoopsMinimal(InsetLoopV, BaseLoopV, StitchResult);

		// set the groups of the new quads along the stitch
		int32 NumNewQuads = StitchResult.NewQuads.Num();
		for (int32 k = 0; k < NumNewQuads; k++)
		{
			Mesh->SetTriangleGroup(StitchResult.NewQuads[k].A, EdgeGroups[k]);
			Mesh->SetTriangleGroup(StitchResult.NewQuads[k].B, EdgeGroups[k]);
		}

		// save the stitch triangles set and associated group IDs
		StitchResult.GetAllTriangles(Region.StitchTriangles[LoopIndex]);
		Region.StitchPolygonIDs[LoopIndex] = NewGroupIDs;

		// for each polygon we created in stitch, set UVs and normals
		// TODO copied from FExtrudeMesh, doesn't really make sense in this context...
		if (Mesh->HasAttributes())
		{
			float AccumUVTranslation = 0;
			FFrame3d FirstProjectFrame;
			FVector3d FrameUp;

			for (int32 k = 0; k < NumNewQuads; k++)
			{
				FVector3f Normal = Editor.ComputeAndSetQuadNormal(StitchResult.NewQuads[k], true);

				// align axis 0 of projection frame to first edge, then for further edges,
				// rotate around 'up' axis to keep normal aligned and frame horizontal
				FFrame3d ProjectFrame;
				if (k == 0)
				{
					FVector3d FirstEdge = Mesh->GetVertex(BaseLoopV[1]) - Mesh->GetVertex(BaseLoopV[0]);
					FirstEdge.Normalize();
					FirstProjectFrame = FFrame3d(FVector3d::Zero(), (FVector3d)Normal);
					FirstProjectFrame.ConstrainedAlignAxis(0, FirstEdge, (FVector3d)Normal);
					FrameUp = FirstProjectFrame.GetAxis(1);
					ProjectFrame = FirstProjectFrame;
				}
				else
				{
					ProjectFrame = FirstProjectFrame;
					ProjectFrame.ConstrainedAlignAxis(2, (FVector3d)Normal, FrameUp);
				}

				if (k > 0)
				{
					AccumUVTranslation += Mesh->GetVertex(BaseLoopV[k]).Distance(Mesh->GetVertex(BaseLoopV[k - 1]));
				}

				// translate horizontally such that vertical spans are adjacent in UV space (so textures tile/wrap properly)
				float TranslateU = UVScaleFactor * AccumUVTranslation;
				Editor.SetQuadUVsFromProjection(StitchResult.NewQuads[k], ProjectFrame, UVScaleFactor, FVector2f(TranslateU, 0));
			}
		}

		Region.BaseLoops[LoopIndex].InitializeFromVertices(Mesh, BaseLoopV);
		Region.InsetLoops[LoopIndex].InitializeFromVertices(Mesh, InsetLoopV);
		LoopIndex++;
	}

	return true;
}


