// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp MeshNormals

#include "MeshNormals.h"
#include "Async/ParallelFor.h"
#include "MeshIndexUtil.h"


void FMeshNormals::SetCount(int Count, bool bClearToZero)
{
	if (Normals.Num() < Count) 
	{
		Normals.SetNumUninitialized(Count);
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

void FMeshNormals::Compute_FaceAvg(bool bWeightByArea, bool bWeightByAngle)
{
	if (!bWeightByAngle && bWeightByArea)
	{
		Compute_FaceAvg_AreaWeighted(); // faster case
		return;
	}

	// most general case
	SetCount(Mesh->MaxVertexID(), true);

	for (int TriIdx : Mesh->TriangleIndicesItr())
	{
		FVector3d TriNormal, TriCentroid; double TriArea;
		Mesh->GetTriInfo(TriIdx, TriNormal, TriArea, TriCentroid);
		FVector3d TriNormalWeights = GetVertexWeightsOnTriangle(Mesh, TriIdx, TriArea, bWeightByArea, bWeightByAngle);

		FIndex3i Triangle = Mesh->GetTriangle(TriIdx);
		Normals[Triangle.A] += TriNormal * TriNormalWeights[0];
		Normals[Triangle.B] += TriNormal * TriNormalWeights[1];
		Normals[Triangle.C] += TriNormal * TriNormalWeights[2];
	}

	for (int VertIdx : Mesh->VertexIndicesItr())
	{
		Normals[VertIdx].Normalize();
	}
}

void FMeshNormals::Compute_Triangle()
{
	int NumTriangles = Mesh->MaxTriangleID();
	SetCount(NumTriangles, false);
	ParallelFor(NumTriangles, [&](int32 Index)
	{
		if (Mesh->IsTriangle(Index))
		{
			Normals[Index] = Mesh->GetTriNormal(Index);
		}
	});
}




void FMeshNormals::Compute_Overlay_FaceAvg(const FDynamicMeshNormalOverlay* NormalOverlay, bool bWeightByArea, bool bWeightByAngle)
{
	if ((!bWeightByAngle) && bWeightByArea)
	{
		Compute_Overlay_FaceAvg_AreaWeighted(NormalOverlay); // faster case
		return;
	}

	// most general case
	SetCount(NormalOverlay->MaxElementID(), true);

	for (int TriIdx : Mesh->TriangleIndicesItr())
	{
		FVector3d TriNormal, TriCentroid; double TriArea;
		Mesh->GetTriInfo(TriIdx, TriNormal, TriArea, TriCentroid);
		FVector3d TriNormalWeights = GetVertexWeightsOnTriangle(Mesh, TriIdx, TriArea, bWeightByArea, bWeightByAngle);

		FIndex3i Tri = NormalOverlay->GetTriangle(TriIdx);
		for (int j = 0; j < 3; ++j) 
		{
			if (Tri[j] != FDynamicMesh3::InvalidID)
			{
				Normals[Tri[j]] += TriNormal * TriNormalWeights[j];
			}
		}
	}

	for (int ElemIdx : NormalOverlay->ElementIndicesItr())
	{
		Normals[ElemIdx].Normalize();
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


void FMeshNormals::QuickComputeVertexNormalsForTriangles(FDynamicMesh3& Mesh, const TArray<int32>& Triangles, bool bWeightByArea, bool bWeightByAngle, bool bInvert)
{
	if (Mesh.HasVertexNormals() == false)
	{
		Mesh.EnableVertexNormals(FVector3f::UnitX());
	}

	TArray<int32> VertexIDs;
	MeshIndexUtil::TriangleToVertexIDs(&Mesh, Triangles, VertexIDs);
	ParallelFor(VertexIDs.Num(), [&](int32 i)
	{
		int32 vid = VertexIDs[i];
		FVector3d VtxNormal = ComputeVertexNormal(Mesh, vid, bWeightByArea, bWeightByAngle);
		Mesh.SetVertexNormal(vid, (FVector3f)VtxNormal);
	});
}



bool FMeshNormals::QuickRecomputeOverlayNormals(FDynamicMesh3& Mesh, bool bInvert)
{
	if (Mesh.HasAttributes() && Mesh.Attributes()->GetNormalLayer(0) != nullptr)
	{
		FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->GetNormalLayer(0);
		FMeshNormals Normals(&Mesh);
		Normals.RecomputeOverlayNormals(NormalOverlay);
		Normals.CopyToOverlay(NormalOverlay, bInvert);
		return true;
	}
	return false;
}


FVector3d FMeshNormals::ComputeVertexNormal(const FDynamicMesh3& Mesh, int VertIdx, bool bWeightByArea, bool bWeightByAngle)
{
	FVector3d SumNormal = FVector3d::Zero();
	Mesh.EnumerateVertexTriangles(VertIdx, [&](int32 TriIdx)
	{
		FVector3d TriNormal, TriCentroid; double TriArea;
		Mesh.GetTriInfo(TriIdx, TriNormal, TriArea, TriCentroid);
		FVector3d TriNormalWeights = GetVertexWeightsOnTriangle(&Mesh, TriIdx, TriArea, bWeightByArea, bWeightByAngle);

		FIndex3i Triangle = Mesh.GetTriangle(TriIdx);
		int32 j = IndexUtil::FindTriIndex(VertIdx, Triangle);
		SumNormal += TriNormal * TriNormalWeights[j];
	});
	return SumNormal.Normalized();
}


FVector3d FMeshNormals::ComputeVertexNormal(const FDynamicMesh3& Mesh, int32 VertIdx, TFunctionRef<bool(int32)> TriangleFilterFunc, bool bWeightByArea, bool bWeightByAngle)
{
	FVector3d NormalSum(0, 0, 0);
	Mesh.EnumerateVertexTriangles(VertIdx, [&](int32 TriIdx)
	{
		if (TriangleFilterFunc(TriIdx))
		{
			FVector3d TriNormal, TriCentroid; double TriArea;
			Mesh.GetTriInfo(TriIdx, TriNormal, TriArea, TriCentroid);
			FVector3d TriNormalWeights = GetVertexWeightsOnTriangle(&Mesh, TriIdx, TriArea, bWeightByArea, bWeightByAngle);

			FIndex3i Triangle = Mesh.GetTriangle(TriIdx);
			int32 j = IndexUtil::FindTriIndex(VertIdx, Triangle);
			NormalSum += TriNormal * TriNormalWeights[j];
		}
	});
	return NormalSum.Normalized();
}



FVector3d FMeshNormals::ComputeOverlayNormal(const FDynamicMesh3& Mesh, FDynamicMeshNormalOverlay* NormalOverlay, int ElemIdx)
{
	int ParentVertexID = NormalOverlay->GetParentVertex(ElemIdx);
	FVector3d SumNormal = FVector3d::Zero();
	int Count = 0;
	Mesh.EnumerateVertexTriangles(ParentVertexID, [&](int32 TriIdx)
	{
		if (NormalOverlay->TriangleHasElement(TriIdx, ElemIdx))
		{
			FVector3d Normal, Centroid; double Area;
			Mesh.GetTriInfo(TriIdx, Normal, Area, Centroid);
			SumNormal += Area * Normal;
			Count++;
		}
	});

	return (Count > 0) ? SumNormal.Normalized() : FVector3d::Zero();
}


void FMeshNormals::InitializeOverlayToPerVertexNormals(FDynamicMeshNormalOverlay* NormalOverlay, bool bUseMeshVertexNormalsIfAvailable)
{
	const FDynamicMesh3* Mesh = NormalOverlay->GetParentMesh();
	bool bUseMeshNormals = bUseMeshVertexNormalsIfAvailable && Mesh->HasVertexNormals();
	FMeshNormals Normals(Mesh);
	if (bUseMeshNormals == false)
	{
		Normals.ComputeVertexNormals();
	}

	NormalOverlay->ClearElements();

	TArray<int> VertToNormalMap;
	VertToNormalMap.SetNumUninitialized(Mesh->MaxVertexID());
	for (int vid : Mesh->VertexIndicesItr())
	{
		FVector3f Normal = (bUseMeshNormals) ? Mesh->GetVertexNormal(vid) : (FVector3f)Normals[vid];
		int nid = NormalOverlay->AppendElement(Normal);
		VertToNormalMap[vid] = nid;
	}

	for (int tid : Mesh->TriangleIndicesItr())
	{
		FIndex3i Tri = Mesh->GetTriangle(tid);
		Tri.A = VertToNormalMap[Tri.A];
		Tri.B = VertToNormalMap[Tri.B];
		Tri.C = VertToNormalMap[Tri.C];
		NormalOverlay->SetTriangle(tid, Tri);
	}
}
