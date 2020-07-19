// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusAction.h"

#include "OptimusNodeGraphActions.generated.h"

class UOptimusNode;
class UOptimusNodeGraph;
class UOptimusNodeLink;
class UOptimusNodePin;

// A base action for adding/removing nodes.
USTRUCT(Atomic)
struct FOptimusNodeGraphAction_AddRemoveNode :
	public FOptimusAction
{
	GENERATED_BODY()

protected:
	bool AddNode(IOptimusNodeGraphCollectionOwner* InRoot);
	bool RemoveNode(IOptimusNodeGraphCollectionOwner* InRoot);

	// The path of the graph the node should be added to.
	FString GraphPath;

	// The class path of the node to add.
	FString NodeClassPath;

	// THe position the node should be added at in the graph.
	FVector2D GraphPosition;

	// The path of the newly added node or the node to remove.
	FString NodePath;
};


USTRUCT()
struct FOptimusNodeGraphAction_AddNode : 
	public FOptimusNodeGraphAction_AddRemoveNode
{
	GENERATED_BODY()

public:
	FOptimusNodeGraphAction_AddNode() = default;

	FOptimusNodeGraphAction_AddNode(
		UOptimusNodeGraph* InGraph,
		const UClass* InNodeClass,
		const FVector2D& InPosition
	);

	/// Called to retrieve the node that was created by DoImpl after it has been called.
	UOptimusNode* GetNode(IOptimusNodeGraphCollectionOwner* InRoot) const;

protected:
	bool Do(IOptimusNodeGraphCollectionOwner* InRoot) override { return AddNode(InRoot); }
	bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) override { return RemoveNode(InRoot); }
};


USTRUCT()
struct FOptimusNodeGraphAction_RemoveNode :
	public FOptimusNodeGraphAction_AddRemoveNode
{
	GENERATED_BODY()

public:
	FOptimusNodeGraphAction_RemoveNode() = default;

	FOptimusNodeGraphAction_RemoveNode(
		UOptimusNode* InNode
	);

protected:
	bool Do(IOptimusNodeGraphCollectionOwner* InRoot) override { return RemoveNode(InRoot); }
	bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) override { return AddNode(InRoot); }
};


// A base action for adding/removing nodes.
USTRUCT(Atomic)
struct FOptimusNodeGraphAction_AddRemoveLink :
	public FOptimusAction
{
	GENERATED_BODY()

	FOptimusNodeGraphAction_AddRemoveLink() = default;

	FOptimusNodeGraphAction_AddRemoveLink(
		UOptimusNodePin* InNodeOutputPin, 
		UOptimusNodePin* InNodeInputPin
		);

protected:
	bool AddLink(IOptimusNodeGraphCollectionOwner* InRoot);
	bool RemoveLink(IOptimusNodeGraphCollectionOwner* InRoot);

	// The path of the output pin on the node to connect/disconnect to/from.
	FString NodeOutputPinPath;

	// The path of the output input on the node to connect/disconnect to/from.
	FString NodeInputPinPath;
};


USTRUCT()
struct FOptimusNodeGraphAction_AddLink :
	public FOptimusNodeGraphAction_AddRemoveLink
{
	GENERATED_BODY()

	FOptimusNodeGraphAction_AddLink() = default;

	FOptimusNodeGraphAction_AddLink(
		UOptimusNodePin* InNodeOutputPin,
		UOptimusNodePin* InNodeInputPin
	);

protected:
	bool Do(IOptimusNodeGraphCollectionOwner* InRoot) override { return AddLink(InRoot); }
	bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) override { return RemoveLink(InRoot); }
};

USTRUCT()
struct FOptimusNodeGraphAction_RemoveLink :
	public FOptimusNodeGraphAction_AddRemoveLink
{
	GENERATED_BODY()

	FOptimusNodeGraphAction_RemoveLink() = default;

	FOptimusNodeGraphAction_RemoveLink(
		UOptimusNodeLink *InLink
	);

protected:
	bool Do(IOptimusNodeGraphCollectionOwner* InRoot) override { return RemoveLink(InRoot); }
	bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) override { return AddLink(InRoot); }
};
