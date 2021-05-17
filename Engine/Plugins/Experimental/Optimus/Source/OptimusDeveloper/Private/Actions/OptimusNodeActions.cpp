// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusNodeActions.h"

#include "IOptimusNodeGraphCollectionOwner.h"
#include "OptimusNode.h"
#include "OptimusNodePin.h"

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
	NodePath = InNode->GetNodePath();
	NewPosition = InPosition;
	OldPosition = InNode->GetGraphPosition();
}

bool FOptimusNodeAction_MoveNode::Do(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNode* Node = InRoot->ResolveNodePath(NodePath);
	if (!Node)
	{
		return false;
	}

	return Node->SetGraphPositionDirect(NewPosition);
}

bool FOptimusNodeAction_MoveNode::Undo(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNode* Node = InRoot->ResolveNodePath(NodePath);
	if (!Node)
	{
		return false;
	}

	return Node->SetGraphPositionDirect(OldPosition);
}


FOptimusNodeAction_SetPinValue::FOptimusNodeAction_SetPinValue(
	UOptimusNodePin* InPin, 
	const FString& InNewValue
	)
{
	if (ensure(InPin) && InPin->GetSubPins().IsEmpty())
	{
		PinPath = InPin->GetPinPath();
		OldValue = InPin->GetValueAsString();
		NewValue = InNewValue;

		SetTitlef(TEXT("Set Value %s"), *InPin->GetPinPath());
	}
}


bool FOptimusNodeAction_SetPinValue::Do(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNodePin* Pin = InRoot->ResolvePinPath(PinPath);
	if (!Pin)
	{
		return false;
	}

	return Pin->SetValueFromStringDirect(NewValue);
}


bool FOptimusNodeAction_SetPinValue::Undo(IOptimusNodeGraphCollectionOwner* InRoot)
{
	UOptimusNodePin* Pin = InRoot->ResolvePinPath(PinPath);
	if (!Pin)
	{
		return false;
	}

	return Pin->SetValueFromStringDirect(OldValue);
}
