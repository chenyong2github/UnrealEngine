// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimBlueprintCompilerHandler_LinkedAnimGraph.h"
#include "AnimGraphNode_LinkedAnimGraphBase.h"
#include "IAnimBlueprintCompilerCreationContext.h"
#include "IAnimBlueprintCompilationContext.h"
#include "IAnimBlueprintGeneratedClassCompiledData.h"

FAnimBlueprintCompilerHandler_LinkedAnimGraph::FAnimBlueprintCompilerHandler_LinkedAnimGraph(IAnimBlueprintCompilerCreationContext& InCreationContext)
{
	InCreationContext.OnPreProcessAnimationNodes().AddRaw(this, &FAnimBlueprintCompilerHandler_LinkedAnimGraph::PreProcessAnimationNodes);
}

void FAnimBlueprintCompilerHandler_LinkedAnimGraph::PreProcessAnimationNodes(TArrayView<UAnimGraphNode_Base*> InAnimNodes, IAnimBlueprintCompilationContext& InCompilationContext, IAnimBlueprintGeneratedClassCompiledData& OutCompiledData)
{
	for(UAnimGraphNode_Base* AnimNode : InAnimNodes)
	{
		if(UAnimGraphNode_LinkedAnimGraphBase* LinkedAnimGraphBase = Cast<UAnimGraphNode_LinkedAnimGraphBase>(AnimNode))
		{
			LinkedAnimGraphBase->AllocatePoseLinks();
		}
	}
}