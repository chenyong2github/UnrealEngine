// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimBlueprintCompilerSubsystem.h"

#include "AnimBlueprintCompilerSubsystem_CachedPose.generated.h"

class UAnimGraphNode_SaveCachedPose;

UCLASS()
class UAnimBlueprintCompilerSubsystem_CachedPose : public UAnimBlueprintCompilerSubsystem
{
	GENERATED_BODY()

public:
	// Get the map of cache name to encountered save cached pose nodes
	const TMap<FString, UAnimGraphNode_SaveCachedPose*>& GetSaveCachedPoseNodes() const;

private:
	// UAnimBlueprintCompilerSubsystem interface
	virtual void PreProcessAnimationNodes(TArrayView<UAnimGraphNode_Base*> InAnimNodes) override;
	virtual void PostProcessAnimationNodes(TArrayView<UAnimGraphNode_Base*> InAnimNodes) override;

	// Builds the update order list for saved pose nodes in this blueprint
	void BuildCachedPoseNodeUpdateOrder();

	// Traverses a graph to collect save pose nodes starting at InRootNode, then processes each node
	void CachePoseNodeOrdering_StartNewTraversal(UAnimGraphNode_Base* InRootNode, TArray<UAnimGraphNode_SaveCachedPose*> &OrderedSavePoseNodes, TArray<UAnimGraphNode_Base*> VisitedRootNodes);

	// Traverses a graph to collect save pose nodes starting at InAnimGraphNode, does NOT process saved pose nodes afterwards
	void CachePoseNodeOrdering_TraverseInternal(UAnimGraphNode_Base* InAnimGraphNode, TArray<UAnimGraphNode_SaveCachedPose*> &OrderedSavePoseNodes);

private:
	// Map of cache name to encountered save cached pose nodes
	TMap<FString, UAnimGraphNode_SaveCachedPose*> SaveCachedPoseNodes;
};