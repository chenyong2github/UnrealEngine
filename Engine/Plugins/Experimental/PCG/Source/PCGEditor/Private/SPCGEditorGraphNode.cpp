// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPCGEditorGraphNode.h"

#include "PCGEditorGraphNode.h"
#include "PCGNode.h"

void SPCGEditorGraphNode::Construct(const FArguments& InArgs, UPCGEditorGraphNode* InNode)
{
	GraphNode = InNode;
	PCGEditorGraphNode = InNode;

	UpdateGraphNode();
}

void SPCGEditorGraphNode::MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty /*= true*/)
{
	SGraphNode::MoveTo(NewPosition, NodeFilter, bMarkDirty);

	if (UPCGNode* PCGNode = PCGEditorGraphNode->GetPCGNode())
	{
		PCGNode->PositionX = PCGEditorGraphNode->NodePosX;
		PCGNode->PositionY = PCGEditorGraphNode->NodePosY;
		PCGNode->MarkPackageDirty();
	}
}
