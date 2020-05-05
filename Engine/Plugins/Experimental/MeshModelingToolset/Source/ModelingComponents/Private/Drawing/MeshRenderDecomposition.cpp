// Copyright Epic Games, Inc. All Rights Reserved. 

#include "Drawing/MeshRenderDecomposition.h"
#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "DynamicMeshAABBTree3.h"
#include "Async/ParallelFor.h"
#include "ComponentSourceInterfaces.h"


void FMeshRenderDecomposition::BuildAssociations(const FDynamicMesh3* Mesh)
{
	TriangleToGroupMap.SetNum(Mesh->MaxTriangleID());

	ParallelFor(Groups.Num(), [&](int32 GroupIndex) 
	{
		const FGroup& Group = GetGroup(GroupIndex);
		for (int32 tid : Group.Triangles)
		{
			TriangleToGroupMap[tid] = GroupIndex;
		}
	});
}




void FMeshRenderDecomposition::BuildMaterialDecomposition(FDynamicMesh3* Mesh, const FComponentMaterialSet* MaterialSet, FMeshRenderDecomposition& Decomp)
{
	check(MaterialSet);
	FDynamicMeshMaterialAttribute* MaterialID = Mesh->Attributes()->GetMaterialID();
	check(MaterialID);

	int32 NumMaterials = MaterialSet->Materials.Num();
	Decomp.Initialize(NumMaterials);
	for (int32 k = 0; k < NumMaterials; ++k)
	{
		FMeshRenderDecomposition::FGroup& Group = Decomp.GetGroup(k);
		Group.Material = MaterialSet->Materials[k];
	}

	for (int32 tid : Mesh->TriangleIndicesItr())
	{
		int32 MatIdx;
		MaterialID->GetValue(tid, &MatIdx);
		if (MatIdx < NumMaterials)
		{
			FMeshRenderDecomposition::FGroup& Group = Decomp.GetGroup(MatIdx);
			Group.Triangles.Add(tid);
		}
	}
}






static void CollectSubDecomposition(
	FDynamicMesh3* Mesh, 
	const TArray<int32>& Triangles, 
	UMaterialInterface* Material, 
	FMeshRenderDecomposition& Decomp, 
	int32 MaxChunkSize,
	FCriticalSection& DecompLock)
{
	int32 MaxTrisPerGroup = MaxChunkSize;
	if (Triangles.Num() < MaxTrisPerGroup)
	{
		int32 i = Decomp.AppendGroup();
		FMeshRenderDecomposition::FGroup& Group = Decomp.GetGroup(i);
		Group.Triangles = Triangles;
		Group.Material = Material;
		return;
	}

	FDynamicMeshAABBTree3 Spatial(Mesh, false);
	Spatial.SetBuildOptions(MaxTrisPerGroup);
	Spatial.Build(Triangles);

	TArray<int32> ActiveSet;
	TArray<int32> SpillSet;

	FDynamicMeshAABBTree3::FTreeTraversal Collector;
	Collector.BeginBoxTrianglesF = [&](int32 BoxID)
	{
		ActiveSet.Reset();
		ActiveSet.Reserve(MaxTrisPerGroup);
	};
	Collector.NextTriangleF = [&](int32 TriangleID)
	{
		ActiveSet.Add(TriangleID);
	};
	Collector.EndBoxTrianglesF = [&](int32 BoxID)
	{
		if (ActiveSet.Num() > 0)
		{
			if (ActiveSet.Num() < 100)
			{
				SpillSet.Append(ActiveSet);
			}
			else
			{
				DecompLock.Lock();
				int32 i = Decomp.AppendGroup();
				FMeshRenderDecomposition::FGroup& Group = Decomp.GetGroup(i);
				DecompLock.Unlock();
				Group.Triangles = MoveTemp(ActiveSet);
				Group.Material = Material;
			}
		}
	};
	Spatial.DoTraversal(Collector);

	if (SpillSet.Num() > 0)
	{
		//UE_LOG(LogTemp, Warning, TEXT("SpillSet Size: %d"), SpillSet.Num());

		DecompLock.Lock();
		int32 i = Decomp.AppendGroup();
		FMeshRenderDecomposition::FGroup& Group = Decomp.GetGroup(i);
		DecompLock.Unlock();
		Group.Triangles = MoveTemp(SpillSet);
		Group.Material = Material;
	}
}







void FMeshRenderDecomposition::BuildChunkedDecomposition(FDynamicMesh3* Mesh, const FComponentMaterialSet* MaterialSet, FMeshRenderDecomposition& Decomp, int32 MaxChunkSize)
{
	TUniquePtr<FMeshRenderDecomposition> MaterialDecomp = MakeUnique<FMeshRenderDecomposition>();
	BuildMaterialDecomposition(Mesh, MaterialSet, *MaterialDecomp);
	int32 NumMatGroups = MaterialDecomp->Num();

	FCriticalSection Lock;
	ParallelFor(NumMatGroups, [&](int32 k)
	{
		const FMeshRenderDecomposition::FGroup& Group = MaterialDecomp->GetGroup(k);
		CollectSubDecomposition(Mesh, Group.Triangles, Group.Material, Decomp, MaxChunkSize, Lock);
	});
}