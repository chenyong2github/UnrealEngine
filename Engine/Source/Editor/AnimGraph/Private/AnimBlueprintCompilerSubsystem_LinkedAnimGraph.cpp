// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimBlueprintCompilerSubsystem_LinkedAnimGraph.h"
#include "AnimGraphNode_LinkedAnimGraphBase.h"

void UAnimBlueprintCompilerSubsystem_LinkedAnimGraph::PreProcessAnimationNodes(TArrayView<UAnimGraphNode_Base*> InAnimNodes)
{
	for(UAnimGraphNode_Base* AnimNode : InAnimNodes)
	{
		if(UAnimGraphNode_LinkedAnimGraphBase* LinkedAnimGraphBase = Cast<UAnimGraphNode_LinkedAnimGraphBase>(AnimNode))
		{
			LinkedAnimGraphBase->AllocatePoseLinks();
		}
	}
}