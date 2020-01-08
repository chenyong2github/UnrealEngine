// Copyright Epic Games, Inc. All Rights Reserved.

#include "FindPolygonsAlgorithm.h"
#include "MeshNormals.h"



FFindPolygonsAlgorithm::FFindPolygonsAlgorithm(FDynamicMesh3* MeshIn)
{
	Mesh = MeshIn;
}


bool FFindPolygonsAlgorithm::FindPolygons(double DotTolerance)
{
	DotTolerance = 1.0 - DotTolerance;

	// compute face normals
	FMeshNormals Normals(Mesh);
	Normals.ComputeTriangleNormals();

	TArray<bool> DoneTriangle;
	DoneTriangle.SetNum(Mesh->MaxTriangleID());
	PolygonGroupIDs.SetNum(Mesh->MaxTriangleID());

	TArray<int> Stack;

	// grow outward from vertices until we have no more left
	for (int TriID : Mesh->TriangleIndicesItr())
	{
		if (DoneTriangle[TriID] == true)
		{
			continue;
		}

		TArray<int> Polygon;
		Polygon.Add(TriID);
		DoneTriangle[TriID] = true;

		Stack.SetNum(0);
		Stack.Add(TriID);
		while (Stack.Num() > 0)
		{
			int CurTri = Stack.Pop(false);
			FIndex3i NbrTris = Mesh->GetTriNeighbourTris(CurTri);
			for (int j = 0; j < 3; ++j)
			{
				if (NbrTris[j] >= 0
					&& DoneTriangle[NbrTris[j]] == false
					&& PolygonGroupIDs[CurTri] == PolygonGroupIDs[NbrTris[j]])
				{
					double Dot = Normals[CurTri].Dot(Normals[NbrTris[j]]);
					if (Dot > DotTolerance)
					{
						Polygon.Add(NbrTris[j]);
						Stack.Add(NbrTris[j]);
						DoneTriangle[NbrTris[j]] = true;
					}
				}
			}
		}

		FoundPolygons.Add(Polygon);
	}

	Mesh->EnableTriangleGroups(0);

	// set groups from polygons
	int NumPolygons = FoundPolygons.Num();
	PolygonTags.SetNum(NumPolygons);
	PolygonNormals.SetNum(NumPolygons);
	for (int PolyIdx = 0; PolyIdx < NumPolygons; PolyIdx++)
	{
		int Count = FoundPolygons[PolyIdx].Num();
		for (int k = 0; k < Count; ++k)
		{
			Mesh->SetTriangleGroup(FoundPolygons[PolyIdx][k], (PolyIdx + 1));
		}
		PolygonTags[PolyIdx] = (PolyIdx + 1);
		PolygonNormals[PolyIdx] = Normals[FoundPolygons[PolyIdx][0]];
	}

	return (NumPolygons > 0);
}



bool FFindPolygonsAlgorithm::FindPolygonEdges()
{
	for (int eid : Mesh->EdgeIndicesItr())
	{
		if (Mesh->IsGroupBoundaryEdge(eid))
		{
			PolygonEdges.Add(eid);
		}
	}
	return (PolygonEdges.Num() > 0);
}
