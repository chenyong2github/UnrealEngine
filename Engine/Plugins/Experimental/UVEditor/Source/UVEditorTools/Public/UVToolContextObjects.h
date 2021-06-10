// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DynamicMeshAABBTree3.h"
#include "Selection/DynamicMeshSelection.h"
#include "GeometryBase.h"

#include "UVToolContextObjects.generated.h"

// TODO: Should this be spread out across multiple files?

PREDECLARE_GEOMETRY(class FDynamicMesh3);
class UInputRouter;
class UWorld;

UCLASS()
class UVEDITORTOOLS_API UUVToolContextObject : public UObject
{
	GENERATED_BODY()
public:
};

UCLASS()
class UVEDITORTOOLS_API UUVToolLivePreviewAPI : public UUVToolContextObject
{
	GENERATED_BODY()
public:

	void Initialize(UWorld* WorldIn, UInputRouter* RouterIn);

	UWorld* GetLivePreviewWorld() { return World.Get(); }
	UInputRouter* GetLivePreviewInputRouter() { return InputRouter.Get(); }
protected:
	UPROPERTY()
	TWeakObjectPtr<UWorld> World;

	UPROPERTY()
	TWeakObjectPtr<UInputRouter> InputRouter;
};

/** Stores a UV mesh selection */
UCLASS()
class UVEDITORTOOLS_API UUVToolMeshSelection : public UUVToolContextObject
{
	GENERATED_BODY()
public:

	TSharedPtr<UE::Geometry::FDynamicMeshSelection, ESPMode::ThreadSafe> Selection 
		= MakeShared<UE::Geometry::FDynamicMeshSelection, ESPMode::ThreadSafe>();
};

/** Stores UV mesh AABB trees */
UCLASS()
class UVEDITORTOOLS_API UUVToolAABBTreeStorage : public UUVToolContextObject
{
	GENERATED_BODY()
public:

	void Set(UE::Geometry::FDynamicMesh3* MeshKey, TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> Tree);

	TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3> Get(UE::Geometry::FDynamicMesh3* MeshKey);

	void Remove(UE::Geometry::FDynamicMesh3* MeshKey);

	void RemoveByPredicate(TUniqueFunction<
		bool(const TPair<UE::Geometry::FDynamicMesh3*, TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3>>&)> Predicate);

	void Empty();

protected:

	TMap<UE::Geometry::FDynamicMesh3*, TSharedPtr<UE::Geometry::FDynamicMeshAABBTree3>> AABBTrees;
};
