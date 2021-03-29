// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusAction.h"

#include "UObject/UnrealNames.h"

#include "OptimusNodeGraphActions.generated.h"

enum class EOptimusNodeGraphType;
class IOptimusNodeGraphCollectionOwner;
class UOptimusNode;
class UOptimusNodeGraph;
class UOptimusNodeLink;
class UOptimusNodePin;


USTRUCT()
struct FOptimusNodeGraphAction_AddGraph :
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusNodeGraphAction_AddGraph() = default;

	FOptimusNodeGraphAction_AddGraph(
	    IOptimusNodeGraphCollectionOwner* InGraphOwner,
		EOptimusNodeGraphType InGraphType,
		FName InGraphName,
		int32 InGraphIndex
		);

	UOptimusNodeGraph* GetGraph(IOptimusNodeGraphCollectionOwner* InRoot) const;

protected:
	bool Do(IOptimusNodeGraphCollectionOwner* InRoot) override;
	bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) override;

private:
	// The type of graph to create
	EOptimusNodeGraphType GraphType;

	// The name of the graph being created.
	FName GraphName;

	// The index of this new graph in the graph stack.
	int32 GraphIndex;

	// The path of the freshly created graph after the first call to Do.
	FString GraphPath;
};


USTRUCT()
struct FOptimusNodeGraphAction_RemoveGraph : 
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusNodeGraphAction_RemoveGraph() = default;

	FOptimusNodeGraphAction_RemoveGraph(
	    UOptimusNodeGraph* InGraph);

protected:
	bool Do(IOptimusNodeGraphCollectionOwner* InRoot) override;
	bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) override;

private:
	// The path of the graph to remove.
	FString GraphPath;

	// The type of graph to reconstruct back to.
	EOptimusNodeGraphType GraphType;

	// The name to reconstruct the node as.
	FName GraphName;
	
	// The absolute evaluation order the graph was in.
	int32 GraphIndex;

	// The stored graph data.
	TArray<uint8> GraphData;
};


USTRUCT()
struct FOptimusNodeGraphAction_RenameGraph : public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusNodeGraphAction_RenameGraph() = default;

	FOptimusNodeGraphAction_RenameGraph(
	    UOptimusNodeGraph* InGraph,
		FName InNewName);

protected:
	bool Do(IOptimusNodeGraphCollectionOwner* InRoot) override;
	bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) override;

private:
	// The path of the graph to rename. This value will change after each rename.
	FString GraphPath;

	// The new name for this graph. This name may be modified to retain namespace unicity.
	FName NewGraphName;

	// The previous name of the graph
	FName OldGraphName;
};


USTRUCT()
struct FOptimusNodeGraphAction_AddNode : 
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusNodeGraphAction_AddNode() = default;

	FOptimusNodeGraphAction_AddNode(
		UOptimusNodeGraph* InGraph,
		const UClass* InNodeClass,
		TFunction<bool(UOptimusNode*)> InConfigureNodeFunc
		);

	/// Called to retrieve the node that was created by DoImpl after it has been called.
	UOptimusNode* GetNode(IOptimusNodeGraphCollectionOwner* InRoot) const;

protected:
	bool Do(IOptimusNodeGraphCollectionOwner* InRoot) override;
	bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) override;

private:
	// The path of the graph the node should be added to.
	FString GraphPath;

	// The class path of the node to add.
	FString NodeClassPath;

	// An optional function called to configure the node after it gets created, but before it
	// gets added to the graph.
	TFunction<bool(UOptimusNode*)> ConfigureNodeFunc;

	// The path of the newly added node or the node to remove.
	FString NodePath;

	// The name of the newly added node. Used if we undo and then redo the action to
	// ensure we reconstruct the node with the same name.
	FName NodeName = NAME_None;
};


USTRUCT()
struct FOptimusNodeGraphAction_RemoveNode :
	public FOptimusAction
{
	GENERATED_BODY()

public:
	FOptimusNodeGraphAction_RemoveNode() = default;

	FOptimusNodeGraphAction_RemoveNode(
		UOptimusNode* InNode
	);

protected:
	bool Do(IOptimusNodeGraphCollectionOwner* InRoot) override;
	bool Undo(IOptimusNodeGraphCollectionOwner* InRoot) override;

private:
	// Path to the node to remove.
	FString NodePath;

	// The path of the graph the node should be added to.
	FString GraphPath;

	// The class path of the node to reconstruct.
	FString NodeClassPath;

	// The name to reconstruct the node as.
	FName NodeName;

	// The stored node data.
	TArray<uint8> NodeData;
};


// A base action for adding/removing nodes.
USTRUCT()
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

// Mark FOptimusNodeGraphAction_AddRemoveLink as pure virtual, so that the UObject machinery
// won't attempt to instantiate it.
template<>
struct TStructOpsTypeTraits<FOptimusNodeGraphAction_AddRemoveLink> :
	TStructOpsTypeTraitsBase2<FOptimusNodeGraphAction_AddRemoveLink>
{
	enum
	{
		WithPureVirtual = true,
    };
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
