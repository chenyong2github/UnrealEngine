// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraphNodeFactory.h"

#include "PCGEditorGraphNode.h"
#include "SPCGEditorGraphNode.h"

TSharedPtr<SGraphNode> FPCGEditorGraphNodeFactory::CreateNode(UEdGraphNode* InNode) const
{
	if (UPCGEditorGraphNode* GraphNode = Cast<UPCGEditorGraphNode>(InNode))
	{
		TSharedRef<SGraphNode> VisualNode =
			SNew(SPCGEditorGraphNode, GraphNode);

		VisualNode->SlatePrepass();

		return VisualNode;
	}

	return nullptr;
}
