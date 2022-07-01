// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSNodeFactories.h"
#include "Dataflow/DataflowSNode.h"
#include "Dataflow/DataflowEdNode.h"
#include "EdGraphNode_Comment.h"
#include "Dataflow/DataflowCommentNode.h"

TSharedPtr<class SGraphNode> FDataflowSNodeFactory::CreateNode(UEdGraphNode* InNode) const
{
	if (UDataflowEdNode* Node = Cast<UDataflowEdNode>(InNode))
	{
		return SNew(SDataflowEdNode, Node);
	}	
	else if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(InNode))
	{
		return SNew(SDataflowEdNodeComment, CommentNode);
	}
	return NULL;
}


