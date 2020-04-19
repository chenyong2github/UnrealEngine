// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMeshAABBTree3.h"
#include "DynamicMeshOctree3.h"
#include "MeshNormals.h"


namespace UE { namespace SculptUtil {



	void RecalculateNormals_Overlay(FDynamicMesh3* Mesh, const TSet<int32>& ModifiedTris, TSet<int32>& VertexSetBuffer, TArray<int32>& NormalsBuffer)
	{
		FDynamicMeshNormalOverlay* Normals = Mesh->HasAttributes() ? Mesh->Attributes()->PrimaryNormals() : nullptr;
		check(Normals != nullptr);

		NormalsBuffer.Reset();
		VertexSetBuffer.Reset();
		for (int32 TriangleID : ModifiedTris)
		{
			FIndex3i TriElems = Normals->GetTriangle(TriangleID);
			for (int32 j = 0; j < 3; ++j)
			{
				int32 elemid = TriElems[j];
				if (VertexSetBuffer.Contains(elemid) == false)
				{
					VertexSetBuffer.Add(elemid);
					NormalsBuffer.Add(elemid);
				}
			}
		}

		ParallelFor(NormalsBuffer.Num(), [&](int32 k) {
			int32 elemid = NormalsBuffer[k];
			FVector3d NewNormal = FMeshNormals::ComputeOverlayNormal(*Mesh, Normals, elemid);
			Normals->SetElement(elemid, (FVector3f)NewNormal);
		});
	}




	void RecalculateNormals_PerVertex(FDynamicMesh3* Mesh, const TSet<int32>& ModifiedTris, TSet<int32>& VertexSetBuffer, TArray<int32>& NormalsBuffer)
	{
		NormalsBuffer.Reset();
		VertexSetBuffer.Reset();
		for (int32 TriangleID : ModifiedTris)
		{
			FIndex3i TriV = Mesh->GetTriangle(TriangleID);
			for (int32 j = 0; j < 3; ++j)
			{
				int32 vid = TriV[j];
				if (VertexSetBuffer.Contains(vid) == false)
				{
					VertexSetBuffer.Add(vid);
					NormalsBuffer.Add(vid);
				}
			}
		}

		ParallelFor(NormalsBuffer.Num(), [&](int32 k) {
			int32 vid = NormalsBuffer[k];
			FVector3d NewNormal = FMeshNormals::ComputeVertexNormal(*Mesh, vid);
			Mesh->SetVertexNormal(vid, (FVector3f)NewNormal);
		});
	}




	void RecalculateROINormals(FDynamicMesh3* Mesh, const TSet<int32>& TriangleROI, TSet<int32>& VertexSetBuffer, TArray<int32>& NormalsBuffer, bool bForceVertex = false)
	{
		if (Mesh->HasAttributes() && Mesh->Attributes()->PrimaryNormals() && bForceVertex == false)
		{
			RecalculateNormals_Overlay(Mesh, TriangleROI, VertexSetBuffer, NormalsBuffer);
		}
		else
		{
			RecalculateNormals_PerVertex(Mesh, TriangleROI, VertexSetBuffer, NormalsBuffer);
		}
	}



/* end namespace UE::SculptUtil */  } }