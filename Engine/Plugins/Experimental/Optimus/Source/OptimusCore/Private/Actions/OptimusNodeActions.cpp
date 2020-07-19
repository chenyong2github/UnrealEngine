// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNodeActions.h"

#include "IOptimusNodeGraphCollectionOwner.h"
#include "OptimusNode.h"

FOptimusNodeAction_RenameNode::FOptimusNodeAction_RenameNode(
	UOptimusNode* InNode, 
	FString InNewName
	)
{
	NodePath = InNode->GetNodePath();
	NewName = FText::FromString(InNewName);
	OldName = InNode->GetDisplayName();

	SetTitlef(TEXT("Rename %s"), *InNode->GetDisplayName().ToString());
}


bool FOptimusNodeAction_RenameNode::Do(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNode *Node = InRoot->ResolveNodePath(NodePath);
	if (!Node)
	{
		return false;
	}

	Node->SetDisplayName(NewName);

	return true;
}


bool FOptimusNodeAction_RenameNode::Undo(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNode* Node = InRoot->ResolveNodePath(NodePath);
	if (!Node)
	{
		return false;
	}

	Node->SetDisplayName(OldName);

	return true;
}


FOptimusNodeAction_MoveNode::FOptimusNodeAction_MoveNode(
	UOptimusNode* InNode,
	const FVector2D& InPosition
)
{

}

bool FOptimusNodeAction_MoveNode::Do(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNode* Node = InRoot->ResolveNodePath(NodePath);
	if (!Node)
	{
		return false;
	}

	Node->SetGraphPosition(NewPosition);

	return true;
}

bool FOptimusNodeAction_MoveNode::Undo(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNode* Node = InRoot->ResolveNodePath(NodePath);
	if (!Node)
	{
		return false;
	}

	return Node->SetGraphPosition(OldPosition);
}
