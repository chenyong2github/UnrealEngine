// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNodeGraph.h"

#include "OptimusDeformer.h"
#include "OptimusNode.h"
#include "OptimusNodeLink.h"
#include "OptimusNodePin.h"
#include "OptimusActionStack.h"
#include "Actions/OptimusNodeActions.h"
#include "Actions/OptimusNodeGraphActions.h"
#include "Nodes/OptimusNode_GetResource.h"
#include "Nodes/OptimusNode_GetVariable.h"
#include "Nodes/OptimusNode_SetResource.h"

#include "Containers/Queue.h"
#include "Templates/Function.h"
#include "UObject/Package.h"

FString UOptimusNodeGraph::GetGraphPath() const
{
	// TBD: Remove this once we have function nodes.
	ensure(GetOuter()->IsA<UOptimusDeformer>());

	return GetName();
}


IOptimusNodeGraphCollectionOwner* UOptimusNodeGraph::GetOwnerCollection() const
{
	return Cast<IOptimusNodeGraphCollectionOwner>(GetOuter());
}


int32 UOptimusNodeGraph::GetGraphIndex() const
{
	ensure(GetOuter()->IsA<UOptimusDeformer>());
	
	UOptimusDeformer* Deformer = Cast<UOptimusDeformer>(GetOuter());
	const TArray<UOptimusNodeGraph*> &Graphs = Deformer->GetGraphs();

	return Graphs.IndexOfByKey(this);
}


FOptimusGraphNotifyDelegate& UOptimusNodeGraph::GetNotifyDelegate()
{
	return GraphNotifyDelegate;
}


UOptimusNode* UOptimusNodeGraph::AddNode(
	const UClass* InNodeClass, 
	const FVector2D& InPosition
	)
{
	FOptimusNodeGraphAction_AddNode *AddNodeAction = new FOptimusNodeGraphAction_AddNode(
		this, InNodeClass, 
		[InPosition](UOptimusNode *InNode) { 
			return InNode->SetGraphPositionDirect(InPosition, /*Notify=*/false); 
		});
	if (!GetActionStack()->RunAction(AddNodeAction))
	{
		return nullptr;
	}

	return AddNodeAction->GetNode(GetActionStack()->GetGraphCollectionRoot());
}


UOptimusNode* UOptimusNodeGraph::AddResourceGetNode(
	UOptimusResourceDescription* InResourceDesc, 
	const FVector2D& InPosition
	)
{
	FOptimusNodeGraphAction_AddNode* AddNodeAction = new FOptimusNodeGraphAction_AddNode(
	    this, UOptimusNode_GetResource::StaticClass(),
	    [InResourceDesc, InPosition](UOptimusNode* InNode) { 
			Cast<UOptimusNode_GetResource>(InNode)->SetResourceDescription(InResourceDesc);
			return InNode->SetGraphPositionDirect(InPosition, /*Notify=*/false); 
		});

	if (!GetActionStack()->RunAction(AddNodeAction))
	{
		return nullptr;
	}

	return AddNodeAction->GetNode(GetActionStack()->GetGraphCollectionRoot());
}


UOptimusNode* UOptimusNodeGraph::AddResourceSetNode(
	UOptimusResourceDescription* InResourceDesc, 
	const FVector2D& InPosition
	)
{
	FOptimusNodeGraphAction_AddNode* AddNodeAction = new FOptimusNodeGraphAction_AddNode(
	    this, UOptimusNode_SetResource::StaticClass(),
	    [InResourceDesc, InPosition](UOptimusNode* InNode) {
		    Cast<UOptimusNode_SetResource>(InNode)->SetResourceDescription(InResourceDesc);
		    return InNode->SetGraphPositionDirect(InPosition, /*Notify=*/false);
	    });

	if (!GetActionStack()->RunAction(AddNodeAction))
	{
		return nullptr;
	}

	return AddNodeAction->GetNode(GetActionStack()->GetGraphCollectionRoot());
}


UOptimusNode* UOptimusNodeGraph::AddVariableGetNode(
	UOptimusVariableDescription* InVariableDesc, 
	const FVector2D& InPosition
	)
{
	FOptimusNodeGraphAction_AddNode* AddNodeAction = new FOptimusNodeGraphAction_AddNode(
	    this, UOptimusNode_GetVariable::StaticClass(),
	    [InVariableDesc, InPosition](UOptimusNode* InNode) {
		    Cast<UOptimusNode_GetVariable>(InNode)->SetVariableDescription(InVariableDesc);
		    return InNode->SetGraphPositionDirect(InPosition, /*Notify=*/false);
	    });

	if (!GetActionStack()->RunAction(AddNodeAction))
	{
		return nullptr;
	}

	return AddNodeAction->GetNode(GetActionStack()->GetGraphCollectionRoot());
}


bool UOptimusNodeGraph::RemoveNode(UOptimusNode* InNode)
{
	if (!InNode)
	{
		return false;
	}

	return RemoveNodes({InNode});
}


bool UOptimusNodeGraph::RemoveNodes(const TArray<UOptimusNode*> &InNodes)
{
	// Validate the input set.
	if (InNodes.Num() == 0)
	{
		return false;
	}

	for (UOptimusNode* Node : InNodes)
	{
		if (Node == nullptr || Node->GetOuter() != this)
		{
			return false;
		}
	}

	FOptimusCompoundAction* Action = new FOptimusCompoundAction;
	if (InNodes.Num() == 1)
	{
		Action->SetTitlef(TEXT("Remove Node"));
	}
	else
	{
		Action->SetTitlef(TEXT("Remove %d Nodes"), InNodes.Num());
	}

	TSet<int32> AllLinkIndexes;

	// Get all unique links for all the given nodes and remove them *before* we remove the nodes.
	for (UOptimusNode* Node : InNodes)
	{
		AllLinkIndexes.Append(GetAllLinkIndexesToNode(Node));
	}

	for (int32 LinkIndex : AllLinkIndexes)
	{
		Action->AddSubAction<FOptimusNodeGraphAction_RemoveLink>(Links[LinkIndex]);
	}

	for (UOptimusNode* Node : InNodes)
	{
		Action->AddSubAction<FOptimusNodeGraphAction_RemoveNode>(Node);
	}

	return GetActionStack()->RunAction(Action);
}


bool UOptimusNodeGraph::AddLink(UOptimusNodePin* InNodeOutputPin, UOptimusNodePin* InNodeInputPin)
{
	if (!InNodeOutputPin || !InNodeInputPin)
	{
		return false;
	}

	if (!InNodeOutputPin->CanCannect(InNodeInputPin))
	{
		// FIXME: We should be able to report back the failure reason.
		return false;
	}

	// Swap them if they're the wrong order -- a genuine oversight.
	if (InNodeOutputPin->GetDirection() == EOptimusNodePinDirection::Input)
	{
		Swap(InNodeOutputPin, InNodeInputPin);
	}

	// Check to see if there's an existing link on the _input_ pin. Output pins can have any
	// number of connections coming out.
	TArray<int32> PinLinks = GetAllLinkIndexesToPin(InNodeInputPin);

	// This shouldn't happen, but we'll cover for it anyway.
	checkSlow(PinLinks.Num() <= 1);

	FOptimusCompoundAction* Action = new FOptimusCompoundAction;
		
	for (int32 LinkIndex : PinLinks)
	{
		Action->AddSubAction<FOptimusNodeGraphAction_RemoveLink>(Links[LinkIndex]);
	}

	FOptimusNodeGraphAction_AddLink  *AddLinkAction = new FOptimusNodeGraphAction_AddLink(InNodeOutputPin, InNodeInputPin);

	Action->SetTitle(AddLinkAction->GetTitle());
	Action->AddSubAction(AddLinkAction);

	return GetActionStack()->RunAction(Action);
}


bool UOptimusNodeGraph::RemoveLink(UOptimusNodePin* InNodeOutputPin, UOptimusNodePin* InNodeInputPin)
{
	if (!InNodeOutputPin || !InNodeInputPin)
	{
		return false;
	}
	
	// Passing in pins of the same direction is a blatant fail.
	if (!ensure(InNodeOutputPin->GetDirection() != InNodeInputPin->GetDirection()))
	{
		return false;
	}

	// Swap them if they're the wrong order -- a genuine oversight.
	if (InNodeOutputPin->GetDirection() == EOptimusNodePinDirection::Input)
	{
		Swap(InNodeOutputPin, InNodeInputPin);
	}


	for (UOptimusNodeLink* Link: Links)
	{
		if (Link->GetNodeOutputPin() == InNodeOutputPin && Link->GetNodeInputPin() == InNodeInputPin)
		{
			return GetActionStack()->RunAction<FOptimusNodeGraphAction_RemoveLink>(Link);
		}
	}

	return false;
}


bool UOptimusNodeGraph::RemoveAllLinks(UOptimusNodePin* InNodePin)
{
	if (!InNodePin)
	{
		return false;
	}

	TArray<int32> LinksToRemove = GetAllLinkIndexesToPin(InNodePin);
	if (LinksToRemove.Num() == 0)
	{
		return false;
	}

	FOptimusCompoundAction* Action = new FOptimusCompoundAction;
	if (LinksToRemove.Num() == 1)
	{
		Action->SetTitlef(TEXT("Remove Link"));
	}
	else
	{
		Action->SetTitlef(TEXT("Remove %d Links"), LinksToRemove.Num());
	}

	for (int32 LinkIndex : LinksToRemove)
	{
		Action->AddSubAction<FOptimusNodeGraphAction_RemoveLink>(Links[LinkIndex]);
	}

	return GetActionStack()->RunAction(Action);
}


UOptimusNode* UOptimusNodeGraph::CreateNodeDirect(
	const UClass* InNodeClass,
	FName InName,
	TFunction<bool(UOptimusNode*)> InConfigureNodeFunc
)
{
	check(InNodeClass->IsChildOf(UOptimusNode::StaticClass()));

	UOptimusNode* NewNode = NewObject<UOptimusNode>(this, InNodeClass, InName);

	// Configure the node as needed.
	if (InConfigureNodeFunc && !InConfigureNodeFunc(NewNode))
	{
		NewNode->Rename(nullptr, GetTransientPackage());
		NewNode->MarkPendingKill();
		return nullptr;
	}

	NewNode->PostCreateNode();

	AddNodeDirect(NewNode);

	return NewNode;
}


bool UOptimusNodeGraph::AddNodeDirect(UOptimusNode* InNode)
{
	if (InNode == nullptr)
	{
		return false;
	}

	// Re-parent this node if it's not owned directly by us.
	if (InNode->GetOuter() != this)
	{
		UOptimusNodeGraph* OtherGraph = Cast<UOptimusNodeGraph>(InNode->GetOuter());

		// We can't re-parent this node if it still has links.
		if (OtherGraph && OtherGraph->GetAllLinkIndexesToNode(InNode).Num() != 0)
		{
			return false;
		}

		InNode->Rename(nullptr, this);
	}

	Nodes.Add(InNode);

	Notify(EOptimusGraphNotifyType::NodeAdded, InNode);

	InNode->MarkPackageDirty();

	return true;
}


bool UOptimusNodeGraph::RemoveNodeDirect(
	UOptimusNode* InNode, 
	bool bFailIfLinks
	)
{
	int32 NodeIndex = Nodes.IndexOfByKey(InNode);

	// We should always have a node, unless the bookkeeping went awry.
	check(NodeIndex != INDEX_NONE);
	if (NodeIndex == INDEX_NONE)
	{
		return false;
	}

	// There should be no links to this node.
	if (bFailIfLinks)
	{
		TArray<int32> LinkIndexes = GetAllLinkIndexesToNode(InNode);
		if (LinkIndexes.Num() != 0)
		{
			return false;
		}
	}
	else
	{ 
		RemoveAllLinksToNodeDirect(InNode);
	}

	Nodes.RemoveAt(NodeIndex);

	Notify(EOptimusGraphNotifyType::NodeRemoved, InNode);

	// Unparent this node to a temporary storage and mark it for kill.
	InNode->Rename(nullptr, GetTransientPackage());
	InNode->MarkPendingKill();

	return true;
}


bool UOptimusNodeGraph::AddLinkDirect(UOptimusNodePin* NodeOutputPin, UOptimusNodePin* NodeInputPin)
{
	if (!NodeOutputPin || !NodeInputPin)
	{
		return false;
	}

	check(NodeOutputPin->GetDirection() == EOptimusNodePinDirection::Output);
	check(NodeInputPin->GetDirection() == EOptimusNodePinDirection::Input);

	if (NodeOutputPin->GetDirection() != EOptimusNodePinDirection::Output ||
		NodeInputPin->GetDirection() != EOptimusNodePinDirection::Input)
	{
		return false;
	}

	if (NodeOutputPin == NodeInputPin || NodeOutputPin->GetNode() == NodeInputPin->GetNode())
	{
		return false;
	}

	// Does this link already exist?
	for (const UOptimusNodeLink* Link : Links)
	{
		if (Link->GetNodeOutputPin() == NodeOutputPin && Link->GetNodeInputPin() == NodeInputPin)
		{
			return false;
		}
	}

	UOptimusNodeLink* NewLink = NewObject<UOptimusNodeLink>(this);
	NewLink->NodeOutputPin = NodeOutputPin;
	NewLink->NodeInputPin = NodeInputPin;
	Links.Add(NewLink);

	Notify(EOptimusGraphNotifyType::LinkAdded, NewLink);

	NewLink->MarkPackageDirty();

	return true;
}


bool UOptimusNodeGraph::RemoveLinkDirect(UOptimusNodePin* InNodeOutputPin, UOptimusNodePin* InNodeInputPin)
{
	if (!InNodeOutputPin || !InNodeInputPin)
	{
		return false;
	}

	check(InNodeOutputPin->GetDirection() == EOptimusNodePinDirection::Output);
	check(InNodeInputPin->GetDirection() == EOptimusNodePinDirection::Input);

	if (InNodeOutputPin->GetDirection() != EOptimusNodePinDirection::Output ||
		InNodeInputPin->GetDirection() != EOptimusNodePinDirection::Input)
	{
		return false;
	}

	for (int32 LinkIndex = 0; LinkIndex < Links.Num(); LinkIndex++)
	{
		const UOptimusNodeLink* Link = Links[LinkIndex];

		if (Link->GetNodeOutputPin() == InNodeOutputPin && Link->GetNodeInputPin() == InNodeInputPin)
		{
			RemoveLinkByIndex(LinkIndex);
			return true;
		}
	}

	return false;
}


bool UOptimusNodeGraph::RemoveAllLinksToPinDirect(UOptimusNodePin* InNodePin)
{
	if (!InNodePin)
	{
		return false;
	}

	TArray<int32> LinksToRemove = GetAllLinkIndexesToPin(InNodePin);

	if (LinksToRemove.Num() == 0)
	{
		return false;
	}

	// Remove the links in reverse order so that we pop off the highest index first.
	for (int32 i = LinksToRemove.Num(); i-- > 0; /**/)
	{
		RemoveLinkByIndex(LinksToRemove[i]);
	}

	return true;
}


bool UOptimusNodeGraph::RemoveAllLinksToNodeDirect(UOptimusNode* InNode)
{
	if (!InNode)
	{
		return false;
	}

	TArray<int32> LinksToRemove = GetAllLinkIndexesToNode(InNode);

	if (LinksToRemove.Num() == 0)
	{
		return false;
	}

	// Remove the links in reverse order so that we pop off the highest index first.
	for (int32 i = LinksToRemove.Num(); i-- > 0; /**/)
	{
		RemoveLinkByIndex(LinksToRemove[i]);
	}

	return true;
}


void UOptimusNodeGraph::RemoveLinkByIndex(int32 LinkIndex)
{
	UOptimusNodeLink* Link = Links[LinkIndex];

	Links.RemoveAt(LinkIndex);

	Notify(EOptimusGraphNotifyType::LinkRemoved, Link);

	// Unparent the link to a temporary storage and mark it for kill.
	Link->Rename(nullptr, GetTransientPackage());
	Link->MarkPendingKill();
}


bool UOptimusNodeGraph::DoesLinkFormCycle(const UOptimusNodePin* InNodeOutputPin, const UOptimusNodePin* InNodeInputPin) const
{
	if (!ensure(InNodeOutputPin != nullptr && InNodeInputPin != nullptr) ||
		!ensure(InNodeOutputPin->GetDirection() == EOptimusNodePinDirection::Output) ||
		!ensure(InNodeInputPin->GetDirection() == EOptimusNodePinDirection::Input) ||
		!ensure(InNodeOutputPin->GetNode()->GetOwningGraph() == InNodeInputPin->GetNode()->GetOwningGraph()))
	{
		// Invalid pins -- no cycle.
		return false;
	}

	// Self-connection is a cycle.
	if (InNodeOutputPin->GetNode() == InNodeInputPin->GetNode())
	{
		return true;
	}

	const UOptimusNode *CycleNode = InNodeOutputPin->GetNode();

	// Crawl forward from the input pin's node to see if we end up hitting the output pin's node.
	TSet<const UOptimusNode *> ProcessedNodes;
	TQueue<int32> QueuedLinks;

	auto EnqueueIndexes = [&QueuedLinks](TArray<int32> InArray) -> void
	{
		for (int32 Index : InArray)
		{
			QueuedLinks.Enqueue(Index);
		}
	};

	// Enqueue as a work set all links going from the output pins of the node.
	EnqueueIndexes(GetAllLinkIndexesToNode(InNodeInputPin->GetNode(), EOptimusNodePinDirection::Output));
	ProcessedNodes.Add(InNodeInputPin->GetNode());

	int32 LinkIndex;
	while (QueuedLinks.Dequeue(LinkIndex))
	{
		const UOptimusNodeLink *Link = Links[LinkIndex];

		const UOptimusNode *NextNode = Link->GetNodeInputPin()->GetNode();

		if (NextNode == CycleNode)
		{
			// We hit the node we want to connect from, so this would cause a cycle.
			return true;
		}

		// If we haven't processed the next node yet, enqueue all its output links and mark
		// this next node as done so we don't process it again.
		if (!ProcessedNodes.Contains(NextNode))
		{
			EnqueueIndexes(GetAllLinkIndexesToNode(NextNode, EOptimusNodePinDirection::Output));
			ProcessedNodes.Add(NextNode);
		}
	}

	// We didn't hit our target node.
	return false;
}


void UOptimusNodeGraph::Notify(EOptimusGraphNotifyType InNotifyType, UObject* InSubject)
{
	GraphNotifyDelegate.Broadcast(InNotifyType, this, InSubject);
}


TArray<int32> UOptimusNodeGraph::GetAllLinkIndexesToNode(
	const UOptimusNode* InNode,
	EOptimusNodePinDirection InDirection
	) const
{
	TArray<int32> LinkIndexes;
	for (int32 LinkIndex = 0; LinkIndex < Links.Num(); LinkIndex++)
	{
		const UOptimusNodeLink* Link = Links[LinkIndex];

		if ((Link->GetNodeOutputPin()->GetNode() == InNode && InDirection != EOptimusNodePinDirection::Input) ||
			(Link->GetNodeInputPin()->GetNode() == InNode && InDirection != EOptimusNodePinDirection::Output))
		{
			LinkIndexes.Add(LinkIndex);
		}
	}

	return LinkIndexes;
}


TArray<int32> UOptimusNodeGraph::GetAllLinkIndexesToNode(const UOptimusNode* InNode) const
{
	return GetAllLinkIndexesToNode(InNode, EOptimusNodePinDirection::Unknown);
}


TArray<int32> UOptimusNodeGraph::GetAllLinkIndexesToPin(UOptimusNodePin* InNodePin)
{
	TArray<int32> LinksToRemove;
	for (int32 LinkIndex = 0; LinkIndex < Links.Num(); LinkIndex++)
	{
		const UOptimusNodeLink* Link = Links[LinkIndex];

		if ((InNodePin->GetDirection() == EOptimusNodePinDirection::Input &&
			Link->GetNodeInputPin() == InNodePin) ||
			(InNodePin->GetDirection() == EOptimusNodePinDirection::Output &&
				Link->GetNodeOutputPin() == InNodePin))
		{
			LinksToRemove.Add(LinkIndex);
		}
	}

	return LinksToRemove;
}


UOptimusActionStack* UOptimusNodeGraph::GetActionStack() const
{
	UOptimusDeformer *Deformer = Cast<UOptimusDeformer>(GetOuter());
	if (!Deformer)
	{
		return nullptr;
	}

	return Deformer->GetActionStack();
}

