// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationGraph.h"

#define LOCTEXT_NAMESPACE "AnimationGraph"

/////////////////////////////////////////////////////
// UAnimationGraph

UAnimationGraph::UAnimationGraph(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UAnimationGraph::GetGraphNodesOfClass(TSubclassOf<UAnimGraphNode_Base> NodeClass, TArray<UAnimGraphNode_Base*>& GraphNodes, bool bIncludeChildClasses /*= true*/)
{
	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
	{
		UEdGraphNode* Node = Nodes[NodeIndex];
		if (UAnimGraphNode_Base* TypedNode = Cast<UAnimGraphNode_Base>(Node))
		{
			UClass* TypedNodeClass = TypedNode->GetClass();
			if (TypedNodeClass == NodeClass || (bIncludeChildClasses && TypedNode->GetClass()->IsChildOf(NodeClass)))
			{				
				GraphNodes.Add(TypedNode);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
