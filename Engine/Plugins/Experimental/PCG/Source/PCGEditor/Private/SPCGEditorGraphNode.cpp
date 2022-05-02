// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPCGEditorGraphNode.h"

#include "PCGEditorGraphNodeBase.h"
#include "PCGNode.h"

void SPCGEditorGraphNode::Construct(const FArguments& InArgs, UPCGEditorGraphNodeBase* InNode)
{
	GraphNode = InNode;
	PCGEditorGraphNode = InNode;

	if (InNode)
	{
		InNode->OnNodeChangedDelegate.BindSP(this, &SPCGEditorGraphNode::OnNodeChanged);
	}

	UpdateGraphNode();
}

void SPCGEditorGraphNode::MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty /*= true*/)
{
	SGraphNode::MoveTo(NewPosition, NodeFilter, bMarkDirty);

	if (UPCGNode* PCGNode = PCGEditorGraphNode->GetPCGNode())
	{
		PCGNode->Modify();
		PCGNode->PositionX = PCGEditorGraphNode->NodePosX;
		PCGNode->PositionY = PCGEditorGraphNode->NodePosY;
	}
}

void SPCGEditorGraphNode::OnNodeChanged()
{
	UpdateGraphNode();
}
