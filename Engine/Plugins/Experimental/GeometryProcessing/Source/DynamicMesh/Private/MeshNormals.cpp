// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp MeshNormals

#include "MeshNormals.h"


void FMeshNormals::SetCount(int Count, bool bClearToZero)
{
	if (Normals.Num() < Count) 
	{
		Normals.SetNum(Count);
	}
	if (bClearToZero)
	{
		for (int i = 0; i < Count; ++i)
		{
			Normals[i] = FVector3d::Zero();
		}
	}
}



void FMeshNormals::CopyToVertexNormals(FDynamicMesh3* SetMesh, bool bInvert) const
{
	if (SetMesh->HasVertexNormals() == false)
	{
		SetMesh->EnableVertexNormals(FVector3f::UnitX());
	}

	float sign = (bInvert) ? -1.0f : 1.0f;
	int N = FMath::Min(Normals.Num(), SetMesh->MaxVertexID());
	for (int vi = 0; vi < N; ++vi)
	{
		if (Mesh->IsVertex(vi) && SetMesh->IsVertex(vi))
		{
			SetMesh->SetVertexNormal(vi, sign * (FVector3f)Normals[vi]);
		}
	}
}



void FMeshNormals::CopyToOverlay(FDynamicMeshNormalOverlay* NormalOverlay, bool bInvert) const
{
	float sign = (bInvert) ? -1.0f : 1.0f;
	for (int ElemIdx : NormalOverlay->ElementIndicesItr())
	{
		NormalOverlay->SetElement(ElemIdx, sign * (FVector3f)Normals[ElemIdx]);
	}
}


void FMeshNormals::Compute_FaceAvg_AreaWeighted()
{
	SetCount(Mesh->MaxVertexID(), true);

	for (int TriIdx : Mesh->TriangleIndicesItr())
	{
		FVector3d TriNormal, TriCentroid; double TriArea;
		Mesh->GetTriInfo(TriIdx, TriNormal, TriArea, TriCentroid);
		TriNormal *= TriArea;

		FIndex3i Triangle = Mesh->GetTriangle(TriIdx);
		Normals[Triangle.A] += TriNormal;
		Normals[Triangle.B] += TriNormal;
		Normals[Triangle.C] += TriNormal;
	}

	for (int VertIdx : Mesh->VertexIndicesItr())
	{
		Normals[VertIdx].Normalize();
	}
}



void FMeshNormals::Compute_Triangle()
{
	SetCount(Mesh->MaxTriangleID(), false);

	for (int TriIdx : Mesh->TriangleIndicesItr())
	{
		Normals[TriIdx] = Mesh->GetTriNormal(TriIdx);
	}
}




void FMeshNormals::Compute_Overlay_FaceAvg_AreaWeighted(const FDynamicMeshNormalOverlay* NormalOverlay)
{
	SetCount(NormalOverlay->MaxElementID(), true);

	for (int TriIdx : Mesh->TriangleIndicesItr())
	{
		FVector3d TriNormal, TriCentroid; double TriArea;
		Mesh->GetTriInfo(TriIdx, TriNormal, TriArea, TriCentroid);
		TriNormal *= TriArea;

		FIndex3i Tri = NormalOverlay->GetTriangle(TriIdx);
		for (int j = 0; j < 3; ++j) 
		{
			if (Tri[j] != FDynamicMesh3::InvalidID)
			{
				Normals[Tri[j]] += TriNormal;
			}
		}
	}

	for (int ElemIdx : NormalOverlay->ElementIndicesItr())
	{
		Normals[ElemIdx].Normalize();
	}
}



void FMeshNormals::QuickComputeVertexNormals(FDynamicMesh3& Mesh, bool bInvert)
{
	FMeshNormals normals(&Mesh);
	normals.ComputeVertexNormals();
	normals.CopyToVertexNormals(&Mesh, bInvert);
}


FVector3d FMeshNormals::ComputeVertexNormal(const FDynamicMesh3& Mesh, int VertIdx)
{
	FVector3d SumNormal = FVector3d::Zero();
	for (int TriIdx : Mesh.VtxTrianglesItr(VertIdx))
	{
		FVector3d Normal, Centroid; double Area;
		Mesh.GetTriInfo(TriIdx, Normal, Area, Centroid);
		SumNormal += Area * Normal;
	}
	return SumNormal.Normalized();
}