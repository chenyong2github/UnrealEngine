// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/SimpleHoleFiller.h"
#include "DynamicMeshEditor.h"
#include "CompGeom/PolygonTriangulation.h"




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

	bool bOK = false;
	if (FillType == EFillType::PolygonEarClipping)
	{
		bOK = Fill_EarClip(GroupID);
	}
	else
	{
		bOK = Fill_Fan(GroupID);
	}

	if (bOK && Mesh->HasAttributes() && Mesh->Attributes()->PrimaryNormals() != nullptr)
	{
		FDynamicMeshEditor Editor(Mesh);
		Editor.SetTriangleNormals(NewTriangles);
	}

	return bOK;
}


bool FSimpleHoleFiller::Fill_Fan(int GroupID)
{
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




bool FSimpleHoleFiller::Fill_EarClip(int GroupID)
{
	TArray<FVector3d> Vertices;
	int32 NumVertices = Loop.GetVertexCount();
	for (int32 i = 0; i < NumVertices; ++i)
	{
		Vertices.Add(Mesh->GetVertex(Loop.Vertices[i]));
	}

	TArray<FIndex3i> Triangles;
	PolygonTriangulation::TriangulateSimplePolygon(Vertices, Triangles);

	for (FIndex3i PolyTriangle : Triangles)
	{
		FIndex3i MeshTriangle(
			Loop.Vertices[PolyTriangle.A],
			Loop.Vertices[PolyTriangle.C],
			Loop.Vertices[PolyTriangle.B]);  // Reversing orientation here!!
		int32 NewTriangle = Mesh->AppendTriangle(MeshTriangle, GroupID);
		if (NewTriangle >= 0)
		{
			NewTriangles.Add(NewTriangle);
		}
	}

	return true;
}