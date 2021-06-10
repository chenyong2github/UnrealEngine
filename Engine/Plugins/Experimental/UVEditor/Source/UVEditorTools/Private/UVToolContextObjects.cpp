// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVToolContextObjects.h"

#include "InputRouter.h" // Need to define this and UWorld so weak pointers know they are UThings
#include "Engine/World.h"

using namespace UE::Geometry;

void UUVToolLivePreviewAPI::Initialize(UWorld* WorldIn, UInputRouter* RouterIn)
{
	World = WorldIn;
	InputRouter = RouterIn;
}

void UUVToolAABBTreeStorage::Set(FDynamicMesh3* MeshKey, TSharedPtr<FDynamicMeshAABBTree3> Tree)
{
	AABBTrees.Add(MeshKey, Tree);
}

TSharedPtr<FDynamicMeshAABBTree3> UUVToolAABBTreeStorage::Get(FDynamicMesh3* MeshKey)
{
	TSharedPtr<FDynamicMeshAABBTree3>* FoundPtr = AABBTrees.Find(MeshKey);
	return FoundPtr ? *FoundPtr : nullptr;
}

void UUVToolAABBTreeStorage::Remove(FDynamicMesh3* MeshKey)
{
	AABBTrees.Remove(MeshKey);
}

void UUVToolAABBTreeStorage::RemoveByPredicate(TUniqueFunction<
	bool(const TPair<FDynamicMesh3*, TSharedPtr<FDynamicMeshAABBTree3>>&)> Predicate)
{
	for (auto It = AABBTrees.CreateIterator(); It; ++It)
	{
		if (Predicate(*It))
		{
			It.RemoveCurrent();
		}
	}
}

void UUVToolAABBTreeStorage::Empty()
{
	AABBTrees.Empty();
}