// Copyright Epic Games, Inc. All Rights Reserved.

#include "FindPolygonsAlgorithm.h"
#include "MeshNormals.h"
#include "Selections/MeshConnectedComponents.h"



FFindPolygonsAlgorithm::FFindPolygonsAlgorithm(FDynamicMesh3* MeshIn)
{
	Mesh = MeshIn;
}



bool FFindPolygonsAlgorithm::FindPolygonsFromUVIslands()
{
	PolygonGroupIDs.SetNum(Mesh->MaxTriangleID());
	const FDynamicMeshUVOverlay* UV = Mesh->Attributes()->GetUVLayer(0);

	FMeshConnectedComponents Components(Mesh);
	Components.FindConnectedTriangles([&](int32 TriIdx0, int32 TriIdx1)
	{
		return UV->AreTrianglesConnected(TriIdx0, TriIdx1);
	});

	int32 NumComponents = Components.Num();
	for (int32 ci = 0; ci < NumComponents; ++ci)
	{
		FoundPolygons.Add(Components.GetComponent(ci).Indices);
	}

	SetGroupsFromPolygons();

	return (FoundPolygons.Num() > 0);
}



bool FFindPolygonsAlgorithm::FindPolygonsFromFaceNormals(double DotTolerance)
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

	SetGroupsFromPolygons();

	return (FoundPolygons.Num() > 0);
}



void FFindPolygonsAlgorithm::SetGroupsFromPolygons()
{
	Mesh->EnableTriangleGroups(0);

	// set groups from polygons
	int NumPolygons = FoundPolygons.Num();
	PolygonTags.SetNum(NumPolygons);
	PolygonNormals.SetNum(NumPolygons);
	// can be parallel for
	for (int PolyIdx = 0; PolyIdx < NumPolygons; PolyIdx++)
	{
		const TArray<int>& Polygon = FoundPolygons[PolyIdx];
		FVector3d AccumNormal(0, 0, 0);
		int NumTriangles = Polygon.Num();
		for (int k = 0; k < NumTriangles; ++k)
		{
			Mesh->SetTriangleGroup(Polygon[k], (PolyIdx + 1));
			AccumNormal += Mesh->GetTriArea(k) * Mesh->GetTriNormal(Polygon[k]);
		}
		PolygonTags[PolyIdx] = (PolyIdx + 1);

		// find a normal if the average failed
		AccumNormal.Normalize();
		int pi = 0;
		while (AccumNormal.Length() < 0.9 && pi < NumTriangles)
		{
			AccumNormal = Mesh->GetTriNormal(Polygon[pi++]);
		}
		if (AccumNormal.Length() < 0.9)
		{
			AccumNormal = FVector3d::UnitY();
		}

		PolygonNormals[PolyIdx] = AccumNormal;
	}
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
