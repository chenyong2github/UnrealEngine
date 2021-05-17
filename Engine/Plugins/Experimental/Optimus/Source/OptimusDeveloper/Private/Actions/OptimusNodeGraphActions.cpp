// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNodeGraphActions.h"

#include "IOptimusNodeGraphCollectionOwner.h"
#include "OptimusHelpers.h"
#include "OptimusNode.h"
#include "OptimusNodeGraph.h"
#include "OptimusNodeLink.h"
#include "OptimusNodePin.h"

#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/UObjectGlobals.h"


// ---- Add graph

FOptimusNodeGraphAction_AddGraph::FOptimusNodeGraphAction_AddGraph(
	IOptimusNodeGraphCollectionOwner* InGraphOwner, 
	EOptimusNodeGraphType InGraphType, 
	FName InGraphName, 
	int32 InGraphIndex
	)
{
	if (ensure(InGraphOwner))
	{
		GraphType = InGraphType;
		GraphName = InGraphName;
		GraphIndex = InGraphIndex;

		SetTitlef(TEXT("Add graph"));
	}
}



UOptimusNodeGraph* FOptimusNodeGraphAction_AddGraph::GetGraph(IOptimusNodeGraphCollectionOwner* InRoot) const
{
	return InRoot->ResolveGraphPath(GraphPath);
}


bool FOptimusNodeGraphAction_AddGraph::Do(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNodeGraph* Graph = InRoot->CreateGraph(GraphType, GraphName, GraphIndex);
	if (Graph)
	{
		if (GraphName == NAME_None)
		{
			GraphName = Graph->GetFName();
		}

		GraphPath = Graph->GetGraphPath();
		return true;
	}
	else
	{
		GraphPath.Reset();
		return false;
	}
}


bool FOptimusNodeGraphAction_AddGraph::Undo(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNodeGraph* Graph = InRoot->ResolveGraphPath(GraphPath);
	if (Graph == nullptr)
	{
		return false;
	}

	return InRoot->RemoveGraph(Graph);
}


// ---- Remove graph

FOptimusNodeGraphAction_RemoveGraph::FOptimusNodeGraphAction_RemoveGraph(UOptimusNodeGraph* InGraph)
{
	if (ensure(InGraph))
	{
		GraphPath = InGraph->GetGraphPath();
		GraphType = InGraph->GetGraphType();
		GraphName = InGraph->GetFName();
		GraphIndex = InGraph->GetGraphIndex();

		SetTitlef(TEXT("Remove graph"));
	}
}

bool FOptimusNodeGraphAction_RemoveGraph::Do(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNodeGraph* Graph = InRoot->ResolveGraphPath(GraphPath);
	if (!Graph)
	{
		return false;
	}

	// Serialize all stored properties and referenced object 
	{
		Optimus::FBinaryObjectWriter GraphArchive(Graph, GraphData);
	}

	return InRoot->RemoveGraph(Graph);
}


bool FOptimusNodeGraphAction_RemoveGraph::Undo(IOptimusNodeGraphCollectionOwner* InRoot)
{
	// Create a graph, but don't add it to the list of used graphs. Otherwise interested parties
	// will be notified with a partially constructed graph.
	UOptimusNodeGraph* Graph = InRoot->CreateGraph(GraphType, GraphName, TOptional<int32>());
	if (Graph == nullptr)
	{
		return false;
	}

	// Unserialize all the stored properties (and sub-objects) back onto the new graph.
	{
		Optimus::FBinaryObjectReader GraphArchive(Graph, GraphData);
	}
	
	// Now add the graph such that interested parties get notified.
	if (InRoot->AddGraph(Graph, GraphIndex))
	{
		return true;
	}
	else
	{
		Graph->Rename(nullptr, GetTransientPackage());
		Graph->MarkPendingKill();
		return false;
	}
}


// ---- Rename graph

FOptimusNodeGraphAction_RenameGraph::FOptimusNodeGraphAction_RenameGraph(UOptimusNodeGraph* InGraph, FName InNewName)
{
	if (ensure(InGraph) && InGraph->GetFName() != InNewName)
	{
		GraphPath = InGraph->GetGraphPath();

		// Ensure the name is unique within our namespace.
		if (StaticFindObject(UOptimusNodeGraph::StaticClass(), InGraph->GetOuter(), *InNewName.ToString()) != nullptr)
		{
			InNewName = MakeUniqueObjectName(InGraph->GetOuter(), UOptimusNodeGraph::StaticClass(), InNewName);
		}

		NewGraphName = InNewName;
		OldGraphName = InGraph->GetFName();

		SetTitlef(TEXT("Rename graph"));
	}
}


bool FOptimusNodeGraphAction_RenameGraph::Do(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNodeGraph* Graph = InRoot->ResolveGraphPath(GraphPath);
	if (!Graph)
	{
		return false;
	}

	return Graph->Rename(*NewGraphName.ToString(), nullptr);
}


bool FOptimusNodeGraphAction_RenameGraph::Undo(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNodeGraph* Graph = InRoot->ResolveGraphPath(GraphPath);
	if (!Graph)
	{
		return false;
	}

	return Graph->Rename(*OldGraphName.ToString(), nullptr);
}


// ---- Add node

FOptimusNodeGraphAction_AddNode::FOptimusNodeGraphAction_AddNode(
    UOptimusNodeGraph* InGraph,
    const UClass* InNodeClass,
    TFunction<bool(UOptimusNode*)> InConfigureNodeFunc)
{
	if (ensure(InGraph != nullptr) && ensure(InNodeClass != nullptr))
	{
		GraphPath = InGraph->GetGraphPath();
		NodeClassPath = InNodeClass->GetPathName();
		ConfigureNodeFunc = InConfigureNodeFunc;

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

	UOptimusNode* Node = Graph->CreateNodeDirect(NodeClass, NodeName, ConfigureNodeFunc);
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


// ---- Remove node

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

	// Take a copy of the node's contents but not sub-data (like pins).
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
				NodeArchive, /* bInLoadIfFindFails=*/true);
		Node->SerializeScriptProperties(NodeProxyArchive);
	}

	// Create the pins.
	Node->PostCreateNode();

	return Graph->AddNodeDirect(Node);
}


// ---- Add/remoe link base

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


// ---- Add link

FOptimusNodeGraphAction_AddLink::FOptimusNodeGraphAction_AddLink(
	UOptimusNodePin* InNodeOutputPin, 
	UOptimusNodePin* InNodeInputPin
	) :
	FOptimusNodeGraphAction_AddRemoveLink(InNodeOutputPin, InNodeInputPin)
{
	// FIXME: Prettier name.
	SetTitlef(TEXT("Add Link"));
}


// ---- Remove link

FOptimusNodeGraphAction_RemoveLink::FOptimusNodeGraphAction_RemoveLink(
	UOptimusNodeLink* InLink
	) :
	FOptimusNodeGraphAction_AddRemoveLink(InLink->GetNodeOutputPin(), InLink->GetNodeInputPin())
{
	SetTitlef(TEXT("Remove Link"));
}

