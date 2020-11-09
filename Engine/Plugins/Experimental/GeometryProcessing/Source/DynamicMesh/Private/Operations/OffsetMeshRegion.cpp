// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/OffsetMeshRegion.h"
#include "MeshNormals.h"
#include "DynamicMeshEditor.h"
#include "Selections/MeshVertexSelection.h"
#include "DynamicMeshChangeTracker.h"
#include "Selections/MeshConnectedComponents.h"
#include "Operations/ExtrudeMesh.h"
#include "DynamicSubmesh3.h"

FOffsetMeshRegion::FOffsetMeshRegion(FDynamicMesh3* mesh) : Mesh(mesh)
{
	OffsetPositionFunc = [this](const FVector3d& Position, const FVector3f& Normal, int VertexID)
	{
		return Position + this->DefaultOffsetDistance * (FVector3d)Normal;
	};
}


bool FOffsetMeshRegion::Apply()
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
	OffsetRegions.SetNum(RegionComponents.Num());
	for (int k = 0; k < RegionComponents.Num(); ++k)
	{
		FOffsetInfo& Region = OffsetRegions[k];
		Region.InitialTriangles = MoveTemp(RegionComponents.Components[k].Indices);
		bool bRegionOK = false;

		TArray<int32> AllTriangles;
		FMeshConnectedComponents::GrowToConnectedTriangles(Mesh, Region.InitialTriangles, AllTriangles);
		if (AllTriangles.Num() == Region.InitialTriangles.Num() && bOffsetFullComponentsAsSolids)
		{
			bRegionOK = ApplySolidExtrude(Region, (bHaveVertexNormals) ? nullptr : &Normals);
		}
		else 
		{
			bRegionOK = ApplyOffset(Region, (bHaveVertexNormals) ? nullptr : &Normals);
		}

		if ( bRegionOK )
		{
			AllModifiedTriangles.Append(Region.InitialTriangles);
			for (TArray<int32>& RegionTris : Region.StitchTriangles)
			{
				AllModifiedTriangles.Append(RegionTris);
			}
		}
		else
		{
			bAllOK = false;
		}
	}

	return bAllOK;


}


bool FOffsetMeshRegion::ApplySolidExtrude(FOffsetInfo& Region, FMeshNormals* UseNormals)
{
	FDynamicSubmesh3 SubmeshCalc(Mesh, Region.InitialTriangles);
	FDynamicMesh3& Submesh = SubmeshCalc.GetSubmesh();

	FExtrudeMesh Extruder(&Submesh);
	Extruder.ExtrudedPositionFunc = this->OffsetPositionFunc;
	Extruder.DefaultExtrudeDistance = this->DefaultOffsetDistance;
	Extruder.UVScaleFactor = this->UVScaleFactor;
	Extruder.IsPositiveOffset = bIsPositiveOffset;

	bool bOK = Extruder.Apply();
	if (bOK == false)
	{
		return false;
	}

	if (ChangeTracker)
	{
		ChangeTracker->SaveTriangles(Region.InitialTriangles, true);
	}

	FDynamicMeshEditor Editor(Mesh);
	Editor.RemoveTriangles(Region.InitialTriangles, true);

	FMeshIndexMappings Mappings;
	Editor.AppendMesh(&Submesh, Mappings);

	// transfer tris and groups back
	// TODO: loops
	for (FExtrudeMesh::FExtrusionInfo& ExtrudeRegionInfo : Extruder.Extrusions)
	{
		for (TArray<int32>& StitchTriSet : ExtrudeRegionInfo.StitchTriangles)
		{
			for (int32 k = 0; k < StitchTriSet.Num(); ++k)
			{
				StitchTriSet[k] = Mappings.GetNewTriangle(StitchTriSet[k]);
			}
			Region.StitchTriangles.Add(StitchTriSet);
		}
		for (TArray<int32>& StitchGroupSet : ExtrudeRegionInfo.StitchPolygonIDs)
		{
			for (int32 k = 0; k < StitchGroupSet.Num(); ++k)
			{
				StitchGroupSet[k] = Mappings.GetNewGroup(StitchGroupSet[k]);
			}
			Region.StitchPolygonIDs.Add(StitchGroupSet);
		}

		for (int32 GroupID : ExtrudeRegionInfo.OffsetTriGroups)
		{
			Region.OffsetGroups.Add(Mappings.GetNewGroup(GroupID));
		}
	}

	Region.bIsSolid = true;

	//Region.BaseLoops[LoopIndex].InitializeFromVertices(Mesh, BaseLoopV);
	//Region.OffsetLoops[LoopIndex].InitializeFromVertices(Mesh, OffsetLoopV);
	//LoopIndex++;

	return true;

}


bool FOffsetMeshRegion::ApplyOffset(FOffsetInfo& Region, FMeshNormals* UseNormals)
{
	FMeshRegionBoundaryLoops InitialLoops(Mesh, Region.InitialTriangles, false);
	bool bOK = InitialLoops.Compute();
	if (bOK == false)
	{
		return false;
	}

	int NumInitialLoops = InitialLoops.GetLoopCount();

	if (ChangeTracker)
	{
		ChangeTracker->SaveTriangles(Region.InitialTriangles, true);
	}

	FDynamicMeshEditor Editor(Mesh);

	// keep track of offset groups
	if (Mesh->HasTriangleGroups())
	{
		for (int32 gid : Region.InitialTriangles)
		{
			Region.OffsetGroups.AddUnique(Mesh->GetTriangleGroup(gid));
		}
	}

	TArray<FDynamicMeshEditor::FLoopPairSet> LoopPairs;
	bOK = Editor.DisconnectTriangles(Region.InitialTriangles, LoopPairs, true);
	if (bOK == false)
	{
		return false;
	}

	// offset vertices
	FMeshVertexSelection SelectionV(Mesh);
	SelectionV.SelectTriangleVertices(Region.InitialTriangles);
	if (bUseFaceNormals)
	{
		TArray<int32> Vertices = SelectionV.AsArray();
		TSet<int32> TriangleSet(Region.InitialTriangles);
		int32 NumV = Vertices.Num();
		TArray<FVector3d> NewPositions;
		NewPositions.SetNum(NumV);
		for (int32 k = 0; k < NumV; ++k)
		{
			int32 vid = Vertices[k];
			FVector3d VertexPos = Mesh->GetVertex(vid);
			FVector3d AccumV = FVector3d::Zero();
			int32 Count = 0;
			for (int32 tid : Mesh->VtxTrianglesItr(vid))
			{
				if (TriangleSet.Contains(tid))
				{
					FVector3f TriNormal = (FVector3f)Mesh->GetTriNormal(tid);
					FVector3d TriNormalOffsetPos = OffsetPositionFunc(VertexPos, TriNormal, vid);
					AccumV += TriNormalOffsetPos;
					Count++;
				}
			}
			NewPositions[k] = (Count == 0) ? VertexPos : (AccumV / (double)Count);
		}
		for (int32 k = 0; k < NumV; ++k)
		{
			Mesh->SetVertex(Vertices[k], NewPositions[k]);
		}
	}
	else
	{
		for (int32 vid : SelectionV)
		{
			FVector3d v = Mesh->GetVertex(vid);
			FVector3f n = (UseNormals != nullptr) ? (FVector3f)(*UseNormals)[vid] : Mesh->GetVertexNormal(vid);
			FVector3d newv = OffsetPositionFunc(v, n, vid);
			Mesh->SetVertex(vid, newv);
		}
	}



	// stitch each loop
	Region.BaseLoops.SetNum(NumInitialLoops);
	Region.OffsetLoops.SetNum(NumInitialLoops);
	Region.StitchTriangles.SetNum(NumInitialLoops);
	Region.StitchPolygonIDs.SetNum(NumInitialLoops);
	int32 LoopIndex = 0;
	for (FDynamicMeshEditor::FLoopPairSet& LoopPair : LoopPairs)
	{
		TArray<int32>& BaseLoopV = LoopPair.OuterVertices;
		TArray<int32>& OffsetLoopV = LoopPair.InnerVertices;
		int NumLoopV = BaseLoopV.Num();

		// allocate a new group ID for each pair of input group IDs, and build up list of new group IDs along loop
		TArray<int32> NewGroupIDs;
		TArray<int32> EdgeGroups;
		TMap<TPair<int32,int32>, int32> NewGroupsMap;
		for (int32 k = 0; k < NumLoopV; ++k)
		{
			int32 OffsetEdgeID = Mesh->FindEdge(OffsetLoopV[k], OffsetLoopV[(k + 1) % NumLoopV]);
			int32 OffsetGroupID = Mesh->GetTriangleGroup(Mesh->GetEdgeT(OffsetEdgeID).A);

			// base edge may not exist if we offset entire region. In that case just use single GroupID
			int32 BaseEdgeID = Mesh->FindEdge(BaseLoopV[k], BaseLoopV[(k + 1) % NumLoopV]);
			int32 BaseGroupID = (BaseEdgeID >= 0) ? Mesh->GetTriangleGroup(Mesh->GetEdgeT(BaseEdgeID).A) : OffsetGroupID;

			TPair<int32,int32> GroupPair(FMathd::Min(BaseGroupID, OffsetGroupID), FMathd::Max(BaseGroupID, OffsetGroupID));
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
		Editor.StitchVertexLoopsMinimal(OffsetLoopV, BaseLoopV, StitchResult);

		// set the groups of the new quads along the stitch
		int NumNewQuads = StitchResult.NewQuads.Num();
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

			for (int k = 0; k < NumNewQuads; k++)
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
		Region.OffsetLoops[LoopIndex].InitializeFromVertices(Mesh, OffsetLoopV);
		LoopIndex++;
	}

	return true;
}


