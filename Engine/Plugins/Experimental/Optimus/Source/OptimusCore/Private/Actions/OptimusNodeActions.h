// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusAction.h"

#include "OptimusNodeActions.generated.h"

class UOptimusNode;
class UOptimusNodePin;

USTRUCT()
struct FOptimusNodeAction_RenameNode : 
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusNodeAction_RenameNode() = default;

	FOptimusNodeAction_RenameNode(
		UOptimusNode* InNode,
		FString InNewName
	);

protected:
	bool Do(IOptimusNodeGraphCollectionOwner* InRoot) override;
	bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) override;

private:
	// The path of the node to be renamed.
	FString NodePath;

	// The node's new name
	FText NewName;

	// The node's old name
	FText OldName;
};


USTRUCT()
struct FOptimusNodeAction_MoveNode :
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusNodeAction_MoveNode() = default;

	FOptimusNodeAction_MoveNode(
		UOptimusNode* InNode,
		const FVector2D &InPosition
	);

protected:
	bool Do(IOptimusNodeGraphCollectionOwner* InRoot) override;
	bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) override;

private:
	// The path of the node to be moved.
	FString NodePath;

	// The node's new position
	FVector2D NewPosition;

	// The node's old position
	FVector2D OldPosition;
};


USTRUCT()
struct FOptimusNodeAction_SetPinValue :
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusNodeAction_SetPinValue() = default;

	FOptimusNodeAction_SetPinValue(
		UOptimusNodePin *InPin,
		const FString &InNewValue
		);

protected:
	bool Do(IOptimusNodeGraphCollectionOwner* InRoot) override;
	bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) override;
	
private:
	// The path of the pin to set the value on
	FString PinPath;

	// The new value to set
	FString NewValue;

	// The old value
	FString OldValue;
};
