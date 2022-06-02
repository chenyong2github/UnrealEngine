// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPCGEditorGraphNode.h"

#include "PCGEditorGraphNodeBase.h"
#include "PCGNode.h"
#include "PCGPin.h"

#include "SGraphPin.h"

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

void SPCGEditorGraphNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	check(PCGEditorGraphNode);
	UPCGNode* PCGNode = PCGEditorGraphNode ? PCGEditorGraphNode->GetPCGNode() : nullptr;
	// Implementation note: we do not distinguish single/multiple pins on the output since that is not relevant
	if (PCGNode && PinToAdd->GetPinObj())
	{
		if (UPCGPin* Pin = PCGNode->GetInputPin(PinToAdd->GetPinObj()->PinName))
		{
			if (Pin->Properties.bAllowMultipleConnections)
			{
				PinToAdd->SetCustomPinIcon(FAppStyle::GetBrush("Graph.ArrayPin.Connected"), FAppStyle::GetBrush("Graph.ArrayPin.Disconnected"));
			}
		}
	}

	SGraphNode::AddPin(PinToAdd);
}

void SPCGEditorGraphNode::OnNodeChanged()
{
	UpdateGraphNode();
}