// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphSNodeFactories.h"
#include "EvalGraph/EvalGraphSNode.h"
#include "EvalGraph/EvalGraphEdNode.h"

TSharedPtr<class SGraphNode> FEvalGraphSNodeFactory::CreateNode(UEdGraphNode* InNode) const
{
	if (UEvalGraphEdNode* Node = Cast<UEvalGraphEdNode>(InNode))
	{
		return SNew(SEvalGraphEdNode, Node);
	}
	return NULL;
}


