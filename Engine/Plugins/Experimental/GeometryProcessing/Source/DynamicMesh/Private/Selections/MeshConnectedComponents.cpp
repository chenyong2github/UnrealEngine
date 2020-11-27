// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selections/MeshConnectedComponents.h"
#include "Algo/Sort.h"



void FMeshConnectedComponents::FindConnectedTriangles(TFunction<bool(int32, int32)> TrisConnectedPredicate)
{
	// initial active set contains all valid triangles
	// active values are as follows:   0: unprocessed,  1: in queue,  2: done,  3: invalid
	TArray<uint8> ActiveSet;
	int NumTriangles = Mesh->MaxTriangleID();
	ActiveSet.Init(255, NumTriangles);
	FInterval1i ActiveRange = FInterval1i::Empty();
	for (int tid = 0; tid < NumTriangles; ++tid)
	{
		if (Mesh->IsTriangle(tid))
		{
			ActiveSet[tid] = 0;
			ActiveRange.Contain(tid);
		}
	}

	FindTriComponents(ActiveRange, ActiveSet, TrisConnectedPredicate);
}



void FMeshConnectedComponents::FindConnectedTriangles(TFunctionRef<bool(int)> IndexFilterFunc, TFunction<bool(int32, int32)> TrisConnectedPredicate)
{
	// initial active set contains all valid triangles
	// active values are as follows:   0: unprocessed,  1: in queue,  2: done,  3: invalid
	TArray<uint8> ActiveSet;
	int NumTriangles = Mesh->MaxTriangleID();
	ActiveSet.Init(255, NumTriangles);
	FInterval1i ActiveRange = FInterval1i::Empty();
	for (int tid = 0; tid < NumTriangles; ++tid)
	{
		if (Mesh->IsTriangle(tid) && IndexFilterFunc(tid))
		{
			ActiveSet[tid] = 0;
			ActiveRange.Contain(tid);
		}
	}

	FindTriComponents(ActiveRange, ActiveSet, TrisConnectedPredicate);
}



void FMeshConnectedComponents::FindConnectedTriangles(const TArray<int>& TriangleROI, TFunction<bool(int32, int32)> TrisConnectedPredicate)
{
	// initial active set contains all valid triangles
	// active values are as follows:   0: unprocessed,  1: in queue,  2: done,  3: invalid
	TArray<uint8> ActiveSet;
	int NumTriangles = Mesh->MaxTriangleID();
	ActiveSet.Init(255, NumTriangles);
	FInterval1i ActiveRange = FInterval1i::Empty();
	for (int tid : TriangleROI)
	{
		if (Mesh->IsTriangle(tid))
		{
			ActiveSet[tid] = 0;
			ActiveRange.Contain(tid);
		}
	}

	FindTriComponents(ActiveRange, ActiveSet, TrisConnectedPredicate);
}




void FMeshConnectedComponents::FindTrianglesConnectedToSeeds(const TArray<int>& SeedTriangles, TFunction<bool(int32, int32)> TrisConnectedPredicate)
{
	// initial active set contains all valid triangles
	// active values are as follows:   0: unprocessed,  1: in queue,  2: done,  3: invalid
	TArray<uint8> ActiveSet;
	int32 NumTriangles = Mesh->MaxTriangleID();
	ActiveSet.Init(255, NumTriangles);
	for (int32 tid = 0; tid < NumTriangles; ++tid)
	{
		if (Mesh->IsTriangle(tid))
		{
			ActiveSet[tid] = 0;
		}
	}

	FindTriComponents(SeedTriangles, ActiveSet, TrisConnectedPredicate);
}





void FMeshConnectedComponents::FindTriComponents(FInterval1i ActiveRange, TArray<uint8>& ActiveSet, TFunction<bool(int32, int32)> TrisConnectedPredicate)
{
	Components.Empty();

	// temporary queue
	TArray<int32> ComponentQueue;
	ComponentQueue.Reserve(256);

	// keep finding valid seed triangles and growing connected components
	// until we are done
	for (int i = ActiveRange.Min; i <= ActiveRange.Max; i++)
	{
		if (ActiveSet[i] != 255)
		{
			int SeedTri = i;
			ComponentQueue.Add(SeedTri);
			ActiveSet[SeedTri] = 1;      // in ComponentQueue

			FComponent* Component = new FComponent();
			if (TrisConnectedPredicate)
			{
				FindTriComponent(Component, ComponentQueue, ActiveSet, TrisConnectedPredicate);
			}
			else
			{
				FindTriComponent(Component, ComponentQueue, ActiveSet);
			}
			Components.Add(Component);

			RemoveFromActiveSet(Component, ActiveSet);

			ComponentQueue.Reset(0);
		}
	}
}




void FMeshConnectedComponents::FindTriComponents(const TArray<int32>& SeedList, TArray<uint8>& ActiveSet, TFunction<bool(int32, int32)> TrisConnectedPredicate)
{
	Components.Empty();

	// temporary queue
	TArray<int32> ComponentQueue;
	ComponentQueue.Reserve(256);

	// keep finding valid seed triangles and growing connected components
	// until we are done
	for ( int32 SeedTri : SeedList )
	{
		if (ActiveSet[SeedTri] != 255)
		{
			ComponentQueue.Add(SeedTri);
			ActiveSet[SeedTri] = 1;      // in ComponentQueue

			FComponent* Component = new FComponent();
			if (TrisConnectedPredicate)
			{
				FindTriComponent(Component, ComponentQueue, ActiveSet, TrisConnectedPredicate);
			}
			else
			{
				FindTriComponent(Component, ComponentQueue, ActiveSet);
			}
			Components.Add(Component);

			RemoveFromActiveSet(Component, ActiveSet);

			ComponentQueue.Reset(0);
		}
	}
}






void FMeshConnectedComponents::FindTriComponent(FComponent* Component, TArray<int32>& ComponentQueue, TArray<uint8>& ActiveSet)
{
	while (ComponentQueue.Num() > 0)
	{
		int32 CurTriangle = ComponentQueue.Pop(false);

		ActiveSet[CurTriangle] = 2;   // tri has been processed
		Component->Indices.Add(CurTriangle);

		FIndex3i TriNbrTris = Mesh->GetTriNeighbourTris(CurTriangle);
		for (int j = 0; j < 3; ++j)
		{
			int NbrTri = TriNbrTris[j];
			if (NbrTri != FDynamicMesh3::InvalidID && ActiveSet[NbrTri] == 0)
			{
				ComponentQueue.Add(NbrTri);
				ActiveSet[NbrTri] = 1;           // in ComponentQueue
			}
		}
	}
}


void FMeshConnectedComponents::FindTriComponent(FComponent* Component, TArray<int32>& ComponentQueue, TArray<uint8>& ActiveSet, 
	TFunctionRef<bool(int32, int32)> TriConnectedPredicate)
{
	while (ComponentQueue.Num() > 0)
	{
		int32 CurTriangle = ComponentQueue.Pop(false);

		ActiveSet[CurTriangle] = 2;   // tri has been processed
		Component->Indices.Add(CurTriangle);

		FIndex3i TriNbrTris = Mesh->GetTriNeighbourTris(CurTriangle);
		for (int j = 0; j < 3; ++j)
		{
			int NbrTri = TriNbrTris[j];
			if (NbrTri != FDynamicMesh3::InvalidID && ActiveSet[NbrTri] == 0 && TriConnectedPredicate(CurTriangle, NbrTri))
			{
				ComponentQueue.Add(NbrTri);
				ActiveSet[NbrTri] = 1;           // in ComponentQueue
			}
		}
	}
}



void FMeshConnectedComponents::RemoveFromActiveSet(const FComponent* Component, TArray<uint8>& ActiveSet)
{
	int32 ComponentSize = Component->Indices.Num();
	for (int32 j = 0; j < ComponentSize; ++j)
	{
		ActiveSet[Component->Indices[j]] = 255;
	}
}




int32 FMeshConnectedComponents::GetLargestIndexByCount() const
{
	if (Components.Num() == 0)
	{
		return -1;
	}

	int LargestIdx = 0;
	int LargestCount = Components[LargestIdx].Indices.Num();
	int NumComponents = Components.Num();
	for (int i = 1; i < NumComponents; ++i) {
		if (Components[i].Indices.Num() > LargestCount) {
			LargestCount = Components[i].Indices.Num();
			LargestIdx = i;
		}
	}
	return LargestIdx;
}



void FMeshConnectedComponents::SortByCount(bool bLargestFirst)
{
	TArrayView<FComponent*> View( Components.GetData(), Components.Num() );
	if (bLargestFirst)
	{
		View.StableSort(
			[](const FComponent& A, const FComponent& B) { return (A).Indices.Num() > (B).Indices.Num(); }
		);
	}
	else
	{
		View.StableSort(
			[](const FComponent& A, const FComponent& B) { return (A).Indices.Num() < (B).Indices.Num(); }
		);
	}
}



void FMeshConnectedComponents::GrowToConnectedTriangles(const FDynamicMesh3* Mesh,
	const TArray<int>& InputROI, TArray<int>& ResultROI,
	TArray<int32>* QueueBuffer, 
	TSet<int32>* DoneBuffer,
	TFunctionRef<bool(int32, int32)> CanGrowPredicate
)
{
	TArray<int32> LocalQueue;
	QueueBuffer = (QueueBuffer == nullptr) ? &LocalQueue : QueueBuffer;
	QueueBuffer->Reset(); 
	QueueBuffer->Insert(InputROI, 0);

	TSet<int32> LocalDone;
	DoneBuffer = (DoneBuffer == nullptr) ? &LocalDone : DoneBuffer;
	DoneBuffer->Reset(); 
	DoneBuffer->Append(InputROI);

	while (QueueBuffer->Num() > 0)
	{
		int32 CurTri = QueueBuffer->Pop(false);
		ResultROI.Add(CurTri);

		FIndex3i NbrTris = Mesh->GetTriNeighbourTris(CurTri);
		for (int j = 0; j < 3; ++j)
		{
			int32 tid = NbrTris[j];
			if (tid != FDynamicMesh3::InvalidID && DoneBuffer->Contains(tid) == false && CanGrowPredicate(CurTri, tid))
			{
				QueueBuffer->Add(tid);
				DoneBuffer->Add(tid);
			}
		}
	}
}



void FMeshConnectedComponents::GrowToConnectedTriangles(const FDynamicMesh3* Mesh,
	const TArray<int>& InputROI, TSet<int>& ResultROI,
	TArray<int32>* QueueBuffer,
	TFunctionRef<bool(int32, int32)> CanGrowPredicate
)
{
	TArray<int32> LocalQueue;
	QueueBuffer = (QueueBuffer == nullptr) ? &LocalQueue : QueueBuffer;
	QueueBuffer->Reset();
	QueueBuffer->Insert(InputROI, 0);

	ResultROI.Reset();
	ResultROI.Append(InputROI);

	while (QueueBuffer->Num() > 0)
	{
		int32 CurTri = QueueBuffer->Pop(false);
		ResultROI.Add(CurTri);

		FIndex3i NbrTris = Mesh->GetTriNeighbourTris(CurTri);
		for (int j = 0; j < 3; ++j)
		{
			int32 tid = NbrTris[j];
			if (tid != FDynamicMesh3::InvalidID && ResultROI.Contains(tid) == false && CanGrowPredicate(CurTri, tid))
			{
				QueueBuffer->Add(tid);
				ResultROI.Add(tid);
			}
		}
	}
}