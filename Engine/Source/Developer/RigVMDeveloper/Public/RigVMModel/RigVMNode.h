// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMPin.h"
#include "RigVMNode.generated.h"

class URigVMGraph;

/**
 * The Node represents a single statement within a Graph. 
 * Nodes can represent values such as Variables / Parameters,
 * they can represent Function Invocations or Control Flow
 * logic statements (such as If conditions of For loops).
 * Additionally Nodes are used to represent Comment statements.
 * Nodes contain Pins to represent parameters for Function
 * Invocations or Value access on Variables / Parameters.
 */
UCLASS(BlueprintType)
class RIGVMDEVELOPER_API URigVMNode : public UObject
{
	GENERATED_BODY()

public:

	// Default constructor
	URigVMNode();

	// Default destructor
	virtual ~URigVMNode();

	// Returns the a . separated string containing all of the
	// names used to reach this Node within the Graph.
	// (for now this is the same as the Node's name)
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	FString GetNodePath() const;

	// Returns the current index of the Node within the Graph.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	int32 GetNodeIndex() const;

	// Returns the current index of the instruction in the stack (or INDEX_NONE)
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	int32 GetInstructionIndex() const;

	// Returns the index of the block this node belongs to
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	int32 GetBlockIndex() const;

	// Returns all of the top-level Pins of this Node.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	const TArray<URigVMPin*>& GetPins() const;

	// Returns all of the Pins of this Node (including SubPins).
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	TArray<URigVMPin*> GetAllPinsRecursively() const;

	// Returns a Pin given it's partial pin path below
	// this node (for example: "Color.R")
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	URigVMPin* FindPin(const FString& InPinPath) const;

	// Returns the Graph of this Node
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	URigVMGraph* GetGraph() const;

	// Returns the injection info of this Node (or nullptr)
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	URigVMInjectionInfo* GetInjectionInfo() const;

	// Returns the title of this Node - used for UI.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual FString GetNodeTitle() const;

	// Returns the 2d position of this node - used for UI.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	FVector2D GetPosition() const;

	// Returns the 2d size of this node - used for UI.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	FVector2D GetSize() const;

	// Returns the color of this node - used for UI.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual FLinearColor GetNodeColor() const;

	// Returns the tooltip of this node
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual FText GetToolTipText() const;

	// Returns true if this Node is currently selected.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	bool IsSelected() const;

	// Returns true if this is an injected node.
	// Injected nodes are managed by pins are are not visible to the user.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	bool IsInjected() const;

	// Returns true if this should be visible in the UI
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	bool IsVisibleInUI() const;

	// Returns true if this Node has no side-effects
	// and no internal state.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool IsPure() const;

	// Returns true if the node is defined as non-varying
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool IsDefinedAsConstant() const { return false; }

	// Returns true if the node is defined as non-varying
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool IsDefinedAsVarying() const { return false; }

	// Returns true if this Node has side effects or
	// internal state.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool IsMutable() const;

	virtual bool ContributesToResult() const { return IsMutable(); }

	// Returns true if this Node is the beginning of a scope
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool IsEvent() const;

	// Returns the name of the event
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual FName GetEventName() const;

	// Returns true if the node has any input pins
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool HasInputPin(bool bIncludeIO = true) const;

	// Returns true if the node has any io pins
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool HasIOPin() const;

	// Returns true if the node has any output pins
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool HasOutputPin(bool bIncludeIO = true) const;

	// Returns true if the node has any pins of the provided direction
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	virtual bool HasPinOfDirection(ERigVMPinDirection InDirection) const;

	// Returns true if this Node is linked to another 
	// given node through any of the Nodes' Pins.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	bool IsLinkedTo(URigVMNode* InNode) const;

	// Returns a list of Nodes connected as sources to
	// this Node as the target.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	TArray<URigVMNode*> GetLinkedSourceNodes() const;

	// Returns a list of Nodes connected as targets to
	// this Node as the source.
	UFUNCTION(BlueprintCallable, Category = RigVMNode)
	TArray<URigVMNode*> GetLinkedTargetNodes() const;

	// Returns the name of the slice context for a pin
	virtual FName GetSliceContextForPin(URigVMPin* InRootPin, const FRigVMUserDataArray& InUserData);

	// returns the number of slices on this node
	int32 GetNumSlices(const FRigVMUserDataArray& InUserData);

	// Returns the number of slices for a given context
	virtual int32 GetNumSlicesForContext(const FName& InContextName, const FRigVMUserDataArray& InUserData);

private:

	static const FString NodeColorName;

	bool IsLinkedToRecursive(URigVMPin* InPin, URigVMNode* InNode) const;
	void GetLinkedNodesRecursive(URigVMPin* InPin, bool bLookForSources, TArray<URigVMNode*>& OutNodes) const;

protected:

	virtual FText GetToolTipTextForPin(const URigVMPin* InPin) const;
	virtual bool AllowsLinksOn(const URigVMPin* InPin) const { return true; }

	UPROPERTY()
	FString NodeTitle;

	UPROPERTY()
	FVector2D Position;

	UPROPERTY()
	FVector2D Size;

	UPROPERTY()
	FLinearColor NodeColor;

private:
	UPROPERTY(transient)
	int32 InstructionIndex;

	UPROPERTY(transient)
	int32 BlockIndex;

	UPROPERTY()
	TArray<URigVMPin*> Pins;

	int32 GetSliceContextBracket;

	friend class URigVMController;
	friend class URigVMGraph;
	friend class URigVMPin;
	friend class URigVMCompiler;
	friend class FRigVMLexer;
};

