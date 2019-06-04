// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Operations/ExtrudeMesh.h"
#include "MeshNormals.h"
#include "DynamicMeshEditor.h"


FExtrudeMesh::FExtrudeMesh(FDynamicMesh3* mesh) : Mesh(mesh)
{
	ExtrudedPositionFunc = [this](const FVector3d& Position, const FVector3f& Normal, int VertexID) 
	{
		return Position + this->DefaultExtrudeDistance * Normal;
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
			int NumNewQuads = StitchResult.NewQuads.Num();
			for (int k = 0; k < NumNewQuads; k++)
			{
				FVector3f Normal = Editor.ComputeAndSetQuadNormal(StitchResult.NewQuads[k], true);

				// @todo is there a simpler way to construct rotation from 3 known axes (third id Normal.Cross(UnitY))
				//  (converting from matrix might end up being more efficient due to trig in ConstrainedAlignAxis?)
				FFrame3f ProjectFrame(FVector3f::Zero(), Normal);
				if (FMathd::Abs(ProjectFrame.Y().Dot(FVector3f::UnitY())) < 0.01)
				{
					ProjectFrame.ConstrainedAlignAxis(0, FVector3f::UnitX(), ProjectFrame.Z());
				}
				else
				{
					ProjectFrame.ConstrainedAlignAxis(1, FVector3f::UnitY(), ProjectFrame.Z());
				}
				Editor.SetQuadUVsFromProjection(StitchResult.NewQuads[k], ProjectFrame, UVScaleFactor);
			}
		}

		NewLoops[LoopIndex].InitializeFromVertices(Mesh, OffsetLoop);
		LoopIndex++;
	}

	return true;
}


