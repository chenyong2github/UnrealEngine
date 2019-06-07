// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Operations/SimpleHoleFiller.h"
#include "DynamicMeshEditor.h"

bool FSimpleHoleFiller::Fill(int GroupID)
{
	if (Loop.GetVertexCount() < 3)
	{
		return false;
	}

	// this just needs one triangle
	if (Loop.GetVertexCount() == 3)
	{
		FIndex3i Tri(Loop.Vertices[0], Loop.Vertices[2], Loop.Vertices[1]);
		int NewTID = Mesh->AppendTriangle(Tri, GroupID);
		if (NewTID < 0)
		{
			return false;
		}
		NewTriangles = { NewTID };
		NewVertex = FDynamicMesh3::InvalidID;
		return true;
	}

	// [TODO] 4-case? could check nbr normals to figure out best internal edge...

	// compute centroid
	FVector3d c = FVector3d::Zero();
	for (int i = 0; i < Loop.GetVertexCount(); ++i)
	{
		c += Mesh->GetVertex(Loop.Vertices[i]);
	}
	c *= 1.0 / Loop.GetVertexCount();

	// add centroid vtx
	NewVertex = Mesh->AppendVertex(c);

	// stitch triangles
	FDynamicMeshEditor Editor(Mesh);
	FDynamicMeshEditResult AddFanResult;
	if (!Editor.AddTriangleFan_OrderedVertexLoop(NewVertex, Loop.Vertices, GroupID, AddFanResult))
	{
		Mesh->RemoveVertex(NewVertex, true, false);
		NewVertex = FDynamicMesh3::InvalidID;
		return false;
	}
	NewTriangles = AddFanResult.NewTriangles;

	return true;
}