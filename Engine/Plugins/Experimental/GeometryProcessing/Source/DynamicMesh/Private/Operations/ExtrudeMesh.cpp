// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/ExtrudeMesh.h"
#include "MeshNormals.h"
#include "DynamicMeshEditor.h"


FExtrudeMesh::FExtrudeMesh(FDynamicMesh3* mesh) : Mesh(mesh)
{
	ExtrudedPositionFunc = [this](const FVector3d& Position, const FVector3f& Normal, int VertexID) 
	{
		return Position + this->DefaultExtrudeDistance * (FVector3d)Normal;
	};
}


bool FExtrudeMesh::Apply()
{
	//@todo should apply per-connected-component? will then handle bowties properly

	FMeshNormals Normals;
	bool bHaveVertexNormals = Mesh->HasVertexNormals();
	if (!bHaveVertexNormals)
	{
		Normals = FMeshNormals(Mesh);
		Normals.ComputeVertexNormals();
	}

	InitialLoops.SetMesh(Mesh);
	InitialLoops.Compute();
	int NumInitialLoops = InitialLoops.GetLoopCount();

	BufferUtil::AppendElements(InitialTriangles, Mesh->TriangleIndicesItr());
	BufferUtil::AppendElements(InitialVertices, Mesh->VertexIndicesItr());

	// duplicate triangles of mesh

	FDynamicMeshEditor Editor(Mesh);

	FMeshIndexMappings IndexMap;
	FDynamicMeshEditResult DuplicateResult;
	Editor.DuplicateTriangles(InitialTriangles, IndexMap, DuplicateResult);
	OffsetTriangles = DuplicateResult.NewTriangles;
	OffsetTriGroups = DuplicateResult.NewGroups;
	InitialToOffsetMapV = IndexMap.GetVertexMap().GetForwardMap();

	// set vertices to new positions
	for (int vid : InitialVertices)
	{
		if (!InitialToOffsetMapV.Contains(vid))
		{
			continue;
		}
		int newvid = InitialToOffsetMapV[vid];
		if ( ! Mesh->IsVertex(newvid) )
		{
			continue;
		}

		FVector3d v = Mesh->GetVertex(vid);
		FVector3f n = (bHaveVertexNormals) ? Mesh->GetVertexNormal(vid) : (FVector3f)Normals[vid];
		FVector3d newv = ExtrudedPositionFunc(v, n, vid);

		Mesh->SetVertex(newvid, newv);
	}

	// we need to reverse one side
	if (IsPositiveOffset)
	{
		Editor.ReverseTriangleOrientations(InitialTriangles, true);
	}
	else
	{
		Editor.ReverseTriangleOrientations(OffsetTriangles, true);
	}

	// stitch each loop
	NewLoops.SetNum(NumInitialLoops);
	StitchTriangles.SetNum(NumInitialLoops);
	StitchPolygonIDs.SetNum(NumInitialLoops);
	int LoopIndex = 0;
	for (FEdgeLoop& BaseLoop : InitialLoops.Loops)
	{
		int LoopCount = BaseLoop.GetVertexCount();

		TArray<int> OffsetLoop;
		OffsetLoop.SetNum(LoopCount);
		for (int k = 0; k < LoopCount; ++k)
		{
			OffsetLoop[k] = InitialToOffsetMapV[BaseLoop.Vertices[k]];
		}

		FDynamicMeshEditResult StitchResult;
		if (IsPositiveOffset)
		{
			Editor.StitchVertexLoopsMinimal(OffsetLoop, BaseLoop.Vertices, StitchResult);
		}
		else
		{
			Editor.StitchVertexLoopsMinimal(BaseLoop.Vertices, OffsetLoop, StitchResult);
		}
		StitchResult.GetAllTriangles(StitchTriangles[LoopIndex]);
		StitchPolygonIDs[LoopIndex] = StitchResult.NewGroups;

		// for each polygon we created in stitch, set UVs and normals
		if (Mesh->HasAttributes())
		{
			float AccumUVTranslation = 0;

			FFrame3d FirstProjectFrame;
			FVector3d FrameUp;

			int NumNewQuads = StitchResult.NewQuads.Num();
			for (int k = 0; k < NumNewQuads; k++)
			{
				FVector3f Normal = Editor.ComputeAndSetQuadNormal(StitchResult.NewQuads[k], true);

				// align axis 0 of projection frame to first edge, then for further edges,
				// rotate around 'up' axis to keep normal aligned and frame horizontal
				FFrame3d ProjectFrame;
				if (k == 0)
				{
					FVector3d FirstEdge = Mesh->GetVertex(BaseLoop.Vertices[1]) - Mesh->GetVertex(BaseLoop.Vertices[0]);
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
					AccumUVTranslation += Mesh->GetVertex(BaseLoop.Vertices[k]).Distance(Mesh->GetVertex(BaseLoop.Vertices[k-1]));
				}

				// translate horizontally such that vertical spans are adjacent in UV space (so textures tile/wrap properly)
				float TranslateU = UVScaleFactor*AccumUVTranslation;
				Editor.SetQuadUVsFromProjection(StitchResult.NewQuads[k], ProjectFrame, UVScaleFactor, FVector2f(TranslateU, 0) );
			}
		}

		NewLoops[LoopIndex].InitializeFromVertices(Mesh, OffsetLoop);
		LoopIndex++;
	}

	return true;
}


