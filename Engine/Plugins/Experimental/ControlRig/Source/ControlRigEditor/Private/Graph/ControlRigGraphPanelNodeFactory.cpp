// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigGraphPanelNodeFactory.h"
#include "Graph/ControlRigGraphNode.h"
#include "Graph/ControlRigGraph.h"
#include "EdGraphNode_Comment.h"
#include "SControlRigGraphNode.h"
#include "SControlRigGraphNodeComment.h"
#include "Graph/ControlRigGraphSchema.h"

TSharedPtr<SGraphNode> FControlRigGraphPanelNodeFactory::CreateNode(UEdGraphNode* Node) const
{
	if (UControlRigGraphNode* ControlRigGraphNode = Cast<UControlRigGraphNode>(Node))
	{
		TSharedRef<SGraphNode> GraphNode = 
			SNew(SControlRigGraphNode)
			.GraphNodeObj(ControlRigGraphNode);

		GraphNode->SlatePrepass();
		ControlRigGraphNode->SetDimensions(GraphNode->GetDesiredSize());
		return GraphNode;
	}
	
	if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
	{
		if (CommentNode->GetSchema()->IsA(UControlRigGraphSchema::StaticClass()))
		{
			TSharedRef<SGraphNode> GraphNode =
				SNew(SControlRigGraphNodeComment, CommentNode);

			GraphNode->SlatePrepass();
			return GraphNode;
		}
	}

	return nullptr;
}