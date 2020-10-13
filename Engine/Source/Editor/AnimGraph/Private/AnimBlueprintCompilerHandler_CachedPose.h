// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimGraphNode_Base.h"
#include "IAnimBlueprintCompilerHandler.h"

class UAnimGraphNode_SaveCachedPose;
class IAnimBlueprintCompilerCreationContext;
class IAnimBlueprintCompilationContext;
class IAnimBlueprintGeneratedClassCompiledData;

class FAnimBlueprintCompilerHandler_CachedPose : public IAnimBlueprintCompilerHandler
{
public:
	FAnimBlueprintCompilerHandler_CachedPose(IAnimBlueprintCompilerCreationContext& InCreationContext);

	// Get the map of cache name to encountered save cached pose nodes
	const TMap<FString, UAnimGraphNode_SaveCachedPose*>& GetSaveCachedPoseNodes() const;

private:
	void PreProcessAnimationNodes(TArrayView<UAnimGraphNode_Base*> InAnimNodes, IAnimBlueprintCompilationContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData);
	void PostProcessAnimationNodes(TArrayView<UAnimGraphNode_Base*> InAnimNodes, IAnimBlueprintCompilationContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData);

	// Builds the update order list for saved pose nodes in this blueprint
	void BuildCachedPoseNodeUpdateOrder(IAnimBlueprintCompilationContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData);

	// Traverses a graph to collect save pose nodes starting at InRootNode, then processes each node
	void CachePoseNodeOrdering_StartNewTraversal(IAnimBlueprintCompilationContext& InCompilationContext, UAnimGraphNode_Base* InRootNode, TArray<UAnimGraphNode_SaveCachedPose*> &OrderedSavePoseNodes, TArray<UAnimGraphNode_Base*> VisitedRootNodes);

	// Traverses a graph to collect save pose nodes starting at InAnimGraphNode, does NOT process saved pose nodes afterwards
	void CachePoseNodeOrdering_TraverseInternal(IAnimBlueprintCompilationContext& InCompilationContext, UAnimGraphNode_Base* InAnimGraphNode, TArray<UAnimGraphNode_SaveCachedPose*> &OrderedSavePoseNodes);

private:
	// Map of cache name to encountered save cached pose nodes
	TMap<FString, UAnimGraphNode_SaveCachedPose*> SaveCachedPoseNodes;
};