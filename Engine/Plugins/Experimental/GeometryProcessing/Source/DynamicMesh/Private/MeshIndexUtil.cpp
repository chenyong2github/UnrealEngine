// Copyright Epic Games, Inc. All Rights Reserved.


#include "MeshIndexUtil.h"

void MeshIndexUtil::TriangleToVertexIDs(const FDynamicMesh3* Mesh, const TArray<int>& TriangleIDs, TArray<int>& VertexIDsOut)
{
	UE::MeshIndexUtil::TriangleToVertexIDs(Mesh, TriangleIDs, VertexIDsOut);
}
void UE::MeshIndexUtil::TriangleToVertexIDs(const FDynamicMesh3* Mesh, const TArray<int>& TriangleIDs, TArray<int>& VertexIDsOut)
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




void MeshIndexUtil::VertexToTriangleOneRing(const FDynamicMesh3* Mesh, const TArray<int>& VertexIDs, TSet<int>& TriangleIDsOut)
{
	UE::MeshIndexUtil::VertexToTriangleOneRing(Mesh, VertexIDs, TriangleIDsOut);
}
void UE::MeshIndexUtil::VertexToTriangleOneRing(const FDynamicMesh3* Mesh, const TArray<int>& VertexIDs, TSet<int>& TriangleIDsOut)
{
	// for a TSet it is more efficient to just try to add each triangle twice, than it is to
	// try to avoid duplicate adds with more complex mesh queries
	int32 NumVerts = VertexIDs.Num();
	TriangleIDsOut.Reserve( (NumVerts < 5) ? NumVerts*6 : NumVerts*4);
	for (int32 vid : VertexIDs)
	{
		Mesh->EnumerateVertexEdges(vid, [&](int32 eid) 
		{
			FIndex2i EdgeT = Mesh->GetEdgeT(eid);
			TriangleIDsOut.Add(EdgeT.A);
			if (EdgeT.B != IndexConstants::InvalidID) TriangleIDsOut.Add(EdgeT.B);
		});
	}
}