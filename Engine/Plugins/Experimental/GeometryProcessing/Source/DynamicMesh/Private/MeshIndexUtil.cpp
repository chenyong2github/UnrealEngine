// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "MeshIndexUtil.h"


void MeshIndexUtil::ConverTriangleToVertexIDs(const FDynamicMesh3* Mesh, const TArray<int>& TriangleIDs, TArray<int>& VertexIDsOut)
{
	// if we are getting close to full mesh it is probably more efficient to use a bitmap...

	int NumTris = TriangleIDs.Num();

	// @todo profile this constant
	if (NumTris < 25)
	{
		for (int k = 0; k < NumTris; ++k)
		{
			if (Mesh->IsTriangle(TriangleIDs[k]))
			{
				FIndex3i Tri = Mesh->GetTriangle(TriangleIDs[k]);
				VertexIDsOut.AddUnique(Tri[0]);
				VertexIDsOut.AddUnique(Tri[1]);
				VertexIDsOut.AddUnique(Tri[2]);
			}
		}
	}
	else
	{
		TSet<int> VertexSet;
		VertexSet.Reserve(TriangleIDs.Num()*3);
		for (int k = 0; k < NumTris; ++k)
		{
			if (Mesh->IsTriangle(TriangleIDs[k]))
			{
				FIndex3i Tri = Mesh->GetTriangle(TriangleIDs[k]);
				VertexSet.Add(Tri[0]);
				VertexSet.Add(Tri[1]);
				VertexSet.Add(Tri[2]);
			}
		}

		VertexIDsOut.Reserve(VertexSet.Num());
		for (int VertexID : VertexSet)
		{
			VertexIDsOut.Add(VertexID);
		}
	}
}