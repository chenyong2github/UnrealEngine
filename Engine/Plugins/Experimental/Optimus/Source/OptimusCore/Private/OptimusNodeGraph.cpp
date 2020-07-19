// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNodeGraph.h"

#include "OptimusDeformer.h"
#include "OptimusNode.h"
#include "OptimusNodeLink.h"
#include "OptimusNodePin.h"
#include "OptimusActionStack.h"
#include "Actions/OptimusNodeActions.h"
#include "Actions/OptimusNodeGraphActions.h"

#include "UObject/Package.h"

FString UOptimusNodeGraph::GetGraphPath() const
{
	// TBD: Remove this once we have function nodes.
	ensure(GetOuter()->IsA<UOptimusDeformer>());

	return GetName();
}


FOptimusNodeGraphEvent& UOptimusNodeGraph::OnModify()
{
	return ModifiedEvent;
}


UOptimusNode* UOptimusNodeGraph::AddNode(
	const UClass* InNodeClass, 
	const FVector2D& InPosition
	)
{
	FOptimusNodeGraphAction_AddNode *AddNodeAction = new FOptimusNodeGraphAction_AddNode(this, InNodeClass, InPosition);
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

	check(InNodeOutputPin->GetDirection() == EOptimusNodePinDirection::Output);
	check(InNodeInputPin->GetDirection() == EOptimusNodePinDirection::Input);

	if (InNodeOutputPin->GetDirection() != EOptimusNodePinDirection::Output ||
		InNodeInputPin->GetDirection() != EOptimusNodePinDirection::Input)
	{
		return false;
	}

	return GetActionStack()->RunAction<FOptimusNodeGraphAction_AddLink>(InNodeOutputPin, InNodeInputPin);
}


bool UOptimusNodeGraph::RemoveLink(UOptimusNodePin* InNodeOutputPin, UOptimusNodePin* InNodeInputPin)
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


UOptimusNode* UOptimusNodeGraph::AddNodeDirect(
	const UClass* InNodeClass,
	const FVector2D& InPosition
	)
{
	check(InNodeClass->IsChildOf(UOptimusNode::StaticClass()));

	UOptimusNode* NewNode = NewObject<UOptimusNode>(this, InNodeClass, NAME_None, RF_Transactional);
	NewNode->GraphPosition = InPosition;

	Nodes.Add(NewNode);

	Notify(EOptimusNodeGraphNotifyType::NodeAdded, NewNode);

	NewNode->MarkPackageDirty();

	return NewNode;
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

	Notify(EOptimusNodeGraphNotifyType::NodeRemoved, InNode);

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

	UOptimusNodeLink* NewLink = NewObject<UOptimusNodeLink>(
		this, UOptimusNodeLink::StaticClass(), NAME_None, RF_Transactional);
	NewLink->NodeOutputPin = NodeOutputPin;
	NewLink->NodeInputPin = NodeInputPin;
	Links.Add(NewLink);

	Notify(EOptimusNodeGraphNotifyType::NodeLinkAdded, NewLink);

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


void UOptimusNodeGraph::Notify(EOptimusNodeGraphNotifyType InNotifyType, UObject* InSubject)
{
	ModifiedEvent.Broadcast(InNotifyType, this, InSubject);
}


void UOptimusNodeGraph::RemoveLinkByIndex(int32 LinkIndex)
{
	UOptimusNodeLink* Link = Links[LinkIndex];

	Links.RemoveAt(LinkIndex);

	Notify(EOptimusNodeGraphNotifyType::NodeLinkAdded, Link);

	// Unparent the link to a temporary storage and mark it for kill.
	Link->Rename(nullptr, GetTransientPackage());
	Link->MarkPendingKill();
}


TArray<int32> UOptimusNodeGraph::GetAllLinkIndexesToNode(UOptimusNode* InNode)
{
	TArray<int32> LinkIndexes;
	for (int32 LinkIndex = 0; LinkIndex < Links.Num(); LinkIndex++)
	{
		const UOptimusNodeLink* Link = Links[LinkIndex];

		if (Link->GetNodeOutputPin()->GetNode() == InNode ||
			Link->GetNodeInputPin()->GetNode() == InNode)
		{
			LinkIndexes.Add(LinkIndex);
		}
	}

	return LinkIndexes;
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

