// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNodeGraphActions.h"

#include "IOptimusNodeGraphCollectionOwner.h"
#include "OptimusHelpers.h"
#include "OptimusNode.h"
#include "OptimusNodeGraph.h"
#include "OptimusNodeLink.h"
#include "OptimusNodePin.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/UObjectGlobals.h"

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


bool FOptimusNodeGraphAction_AddNode::Do(IOptimusNodeGraphCollectionOwner* InRoot)
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

	UOptimusNode* Node = Graph->AddNodeDirect(NodeClass, NodeName, &GraphPosition);
	if (!Node)
	{
		return false;
	}

	NodePath = Node->GetNodePath();

	return true;
}


bool FOptimusNodeGraphAction_AddNode::Undo(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNode* Node = InRoot->ResolveNodePath(NodePath);
	if (!Node)
	{
		return false;
	}

	UOptimusNodeGraph* Graph = Node->GetOwningGraph();
	if (!Graph)
	{
		return false;
	}

	// Save the assigned node name for when Do gets called again.
	NodeName = Node->GetFName();

	return Graph->RemoveNodeDirect(Node);
}


FOptimusNodeGraphAction_RemoveNode::FOptimusNodeGraphAction_RemoveNode(UOptimusNode* InNode)
{
	if (ensure(InNode != nullptr))
	{
		NodePath = InNode->GetNodePath();

		GraphPath = InNode->GetOwningGraph()->GetGraphPath();
		NodeName = InNode->GetFName();
		NodeClassPath = InNode->GetClass()->GetPathName();

		// FIXME: Prettier name.
		SetTitlef(TEXT("Remove Node"));
	}
}


bool FOptimusNodeGraphAction_RemoveNode::Do(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNode* Node = InRoot->ResolveNodePath(NodePath);
	if (!Node)
	{
		return false;
	}

	UOptimusNodeGraph* Graph = Node->GetOwningGraph();
	if (!ensure(Graph))
	{
		return false;
	}

	// Take a copy of the node's contents.
	{
		FMemoryWriter NodeArchive(NodeData);
		// This fella does the heavy lifting of serializing object references. 
		// FMemoryWriter and fam do not handle UObject* serialization on their own.
		FObjectAndNameAsStringProxyArchive NodeProxyArchive(
				NodeArchive, /* bInLoadIfFindFails=*/ false);
		Node->SerializeScriptProperties(NodeProxyArchive);
	}

	return Graph->RemoveNodeDirect(Node);
}


bool FOptimusNodeGraphAction_RemoveNode::Undo(IOptimusNodeGraphCollectionOwner* InRoot)
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

	UOptimusNode* Node = NewObject<UOptimusNode>(Graph, NodeClass, NodeName);

	{
		FMemoryReader NodeArchive(NodeData);
		FObjectAndNameAsStringProxyArchive NodeProxyArchive(
			NodeArchive, /* bInLoadIfFindFails=*/ true);
		Node->SerializeScriptProperties(NodeProxyArchive);
	}

	return Graph->AddNodeDirect(Node);
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
		ensure(InNodeOutputPin->GetNode()->GetOwningGraph() == InNodeInputPin->GetNode()->GetOwningGraph())
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

	UOptimusNodeGraph *Graph = InNodeOutputPin->GetNode()->GetOwningGraph();
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

	UOptimusNodeGraph* Graph = InNodeOutputPin->GetNode()->GetOwningGraph();
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
