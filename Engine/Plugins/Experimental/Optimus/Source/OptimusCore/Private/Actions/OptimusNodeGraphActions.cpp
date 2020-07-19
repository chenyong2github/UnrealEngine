// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNodeGraphActions.h"

#include "IOptimusNodeGraphCollectionOwner.h"
#include "OptimusHelpers.h"
#include "OptimusNode.h"
#include "OptimusNodeGraph.h"
#include "OptimusNodeLink.h"
#include "OptimusNodePin.h"


bool FOptimusNodeGraphAction_AddRemoveNode::AddNode(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNodeGraph* Graph = InRoot->ResolveGraphPath(GraphPath);
	if (!Graph)
	{
		return false;
	}
	UClass* NodeClass = Optimus::FindObjectInPackageOrGlobal<UClass>(NodeClassPath);
	if (!NodeClass)
	{
		return false;
	}

	UOptimusNode* Node = Graph->AddNodeDirect(NodeClass, GraphPosition);
	if (!Node)
	{
		return false;
	}

	NodePath = Node->GetNodePath();
	return true;
}


bool FOptimusNodeGraphAction_AddRemoveNode::RemoveNode(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNode* Node = InRoot->ResolveNodePath(NodePath);
	if (!Node)
	{
		return false;
	}

	UOptimusNodeGraph* Graph = Cast<UOptimusNodeGraph>(Node->GetOuter());
	if (!Graph)
	{
		return false;
	}

	return Graph->RemoveNodeDirect(Node);
}




FOptimusNodeGraphAction_AddNode::FOptimusNodeGraphAction_AddNode(
	UOptimusNodeGraph* InGraph, 
	const UClass* InNodeClass,
	const FVector2D& InPosition
)
{
	if (ensure(InGraph != nullptr) && ensure(InNodeClass != nullptr))
	{ 
		GraphPath = InGraph->GetGraphPath();
		NodeClassPath = InNodeClass->GetPathName();
		GraphPosition = InPosition;

		// FIXME: Prettier name.
		SetTitlef(TEXT("Add Node"));
	}
}


UOptimusNode* FOptimusNodeGraphAction_AddNode::GetNode(IOptimusNodeGraphCollectionOwner* InRoot) const
{
	return InRoot->ResolveNodePath(NodePath);
}


FOptimusNodeGraphAction_RemoveNode::FOptimusNodeGraphAction_RemoveNode(UOptimusNode* InNode)
{
	if (ensure(InNode != nullptr))
	{
		UOptimusNodeGraph* Graph = Cast<UOptimusNodeGraph>(InNode->GetOuter());

		// Grab information required to reconstruct the node.
		if (ensure(Graph != nullptr))
		{
			GraphPath = Graph->GetGraphPath();
		}

		NodeClassPath = InNode->GetClass()->GetPathName();
		GraphPosition = InNode->GetGraphPosition();
		NodePath = InNode->GetNodePath();

		// FIXME: Prettier name.
		SetTitlef(TEXT("Remove Node"));
	}
}


FOptimusNodeGraphAction_AddRemoveLink::FOptimusNodeGraphAction_AddRemoveLink(
	UOptimusNodePin* InNodeOutputPin, 
	UOptimusNodePin* InNodeInputPin
	)
{
	if (ensure(InNodeOutputPin != nullptr) && ensure(InNodeInputPin != nullptr) &&
		ensure(InNodeOutputPin->GetDirection() == EOptimusNodePinDirection::Output) && 
		ensure(InNodeInputPin->GetDirection() == EOptimusNodePinDirection::Input) && 
		ensure(InNodeOutputPin->GetNode() != InNodeInputPin->GetNode()) &&
		ensure(InNodeOutputPin->GetNode()->GetGraph() == InNodeInputPin->GetNode()->GetGraph())
		)
	{
		NodeOutputPinPath = InNodeOutputPin->GetPinPath();
		NodeInputPinPath = InNodeInputPin->GetPinPath();
	}
}

bool FOptimusNodeGraphAction_AddRemoveLink::AddLink(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNodePin* InNodeOutputPin = InRoot->ResolvePinPath(NodeOutputPinPath);
	if (InNodeOutputPin == nullptr)
	{
		return false;
	}

	UOptimusNodePin* InNodeInputPin = InRoot->ResolvePinPath(NodeInputPinPath);
	if (InNodeInputPin == nullptr)
	{
		return false;
	}

	UOptimusNodeGraph *Graph = InNodeOutputPin->GetNode()->GetGraph();
	return Graph->AddLinkDirect(InNodeOutputPin, InNodeInputPin);
}


bool FOptimusNodeGraphAction_AddRemoveLink::RemoveLink(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNodePin* InNodeOutputPin = InRoot->ResolvePinPath(NodeOutputPinPath);
	if (InNodeOutputPin == nullptr)
	{
		return false;
	}

	UOptimusNodePin* InNodeInputPin = InRoot->ResolvePinPath(NodeInputPinPath);
	if (InNodeInputPin == nullptr)
	{
		return false;
	}

	UOptimusNodeGraph* Graph = InNodeOutputPin->GetNode()->GetGraph();
	return Graph->RemoveLinkDirect(InNodeOutputPin, InNodeInputPin);
}


FOptimusNodeGraphAction_AddLink::FOptimusNodeGraphAction_AddLink(
	UOptimusNodePin* InNodeOutputPin, 
	UOptimusNodePin* InNodeInputPin
	) :
	FOptimusNodeGraphAction_AddRemoveLink(InNodeOutputPin, InNodeInputPin)
{
	// FIXME: Prettier name.
	SetTitlef(TEXT("Add Link"));
}


FOptimusNodeGraphAction_RemoveLink::FOptimusNodeGraphAction_RemoveLink(
	UOptimusNodeLink* InLink
	) :
	FOptimusNodeGraphAction_AddRemoveLink(InLink->GetNodeOutputPin(), InLink->GetNodeInputPin())
{
	SetTitlef(TEXT("Remove Link"));
}
