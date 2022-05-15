// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSNodeFactories.h"
#include "Dataflow/DataflowSNode.h"
#include "Dataflow/DataflowEdNode.h"

TSharedPtr<class SGraphNode> FDataflowSNodeFactory::CreateNode(UEdGraphNode* InNode) const
{
	if (UDataflowEdNode* Node = Cast<UDataflowEdNode>(InNode))
	{
		return SNew(SDataflowEdNode, Node);
	}
	return NULL;
}


