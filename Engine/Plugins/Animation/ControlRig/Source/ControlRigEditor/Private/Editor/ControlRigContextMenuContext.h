// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Rigs/RigHierarchyDefines.h"

#include "ControlRigContextMenuContext.generated.h"

class UControlRig;
class UControlRigBlueprint;
class FControlRigEditor;
class URigVMGraph;
class URigVMNode;
class URigVMPin;

USTRUCT(BlueprintType)
struct FControlRigRigHierarchyDragAndDropContext
{
	GENERATED_BODY()
	
	FControlRigRigHierarchyDragAndDropContext() = default;
	
	FControlRigRigHierarchyDragAndDropContext(const TArray<FRigElementKey> InDraggedElementKeys, FRigElementKey InTargetElementKey)
		: DraggedElementKeys(InDraggedElementKeys)
		, TargetElementKey(InTargetElementKey)
	{
	}
	
	UPROPERTY(BlueprintReadOnly, Category = ControlRigEditorExtensions)
	TArray<FRigElementKey> DraggedElementKeys;

	UPROPERTY(BlueprintReadOnly, Category = ControlRigEditorExtensions)
	FRigElementKey TargetElementKey;
};

USTRUCT(BlueprintType)
struct FControlRigGraphNodeContextMenuContext
{
	GENERATED_BODY()

	FControlRigGraphNodeContextMenuContext() = default;
	
	FControlRigGraphNodeContextMenuContext(TObjectPtr<const URigVMGraph> InGraph, TObjectPtr<const URigVMNode> InNode, TObjectPtr<const URigVMPin> InPin)
		: Graph(InGraph)
		, Node(InNode)
		, Pin(InPin)
	{
	}
	
	/** The graph associated with this context. */
	UPROPERTY(BlueprintReadOnly, Category = ControlRigEditorExtensions)
	TObjectPtr<const URigVMGraph> Graph;

	/** The node associated with this context. */
	UPROPERTY(BlueprintReadOnly, Category = ControlRigEditorExtensions)
	TObjectPtr<const URigVMNode> Node;

	/** The pin associated with this context; may be NULL when over a node. */
	UPROPERTY(BlueprintReadOnly, Category = ControlRigEditorExtensions)
	TObjectPtr<const URigVMPin> Pin;
};

UCLASS(BlueprintType)
class UControlRigContextMenuContext : public UObject
{
	GENERATED_BODY()

public:
	/**
	 *	Initialize the Context
	 * @param InControlRigBlueprint 	The Control Rig Bluerpint currently opened in the Editor
	 * @param InDragAndDropContext 		Optional, only used for the menu that shows up after a drag & drop action
	 * @param InGraphNodeContext 		Optional, only used for the graph node context menu
	*/
	void Init(TWeakObjectPtr<UControlRigBlueprint> InControlRigBlueprint, const FControlRigRigHierarchyDragAndDropContext& InDragAndDropContext = FControlRigRigHierarchyDragAndDropContext(), const FControlRigGraphNodeContextMenuContext& InGraphNodeContext = FControlRigGraphNodeContextMenuContext());

	/** Get the control rig blueprint that we are editing */
	UFUNCTION(BlueprintCallable, Category = ControlRigEditorExtensions)
    UControlRigBlueprint* GetControlRigBlueprint() const;
	
	/** Get the active control rig instance in the viewport */
	UFUNCTION(BlueprintCallable, Category = ControlRigEditorExtensions)
    UControlRig* GetControlRig() const;

	/** Returns true if either alt key is down */
	UFUNCTION(BlueprintCallable, Category = ControlRigEditorExtensions)
	bool IsAltDown() const;
	
	/** Returns context for a drag & drop action that contains source and target element keys */ 
	UFUNCTION(BlueprintCallable, Category = ControlRigEditorExtensions)
	FControlRigRigHierarchyDragAndDropContext GetRigHierarchyDragAndDropContext();

	/** Returns context for graph node context menu */
	UFUNCTION(BlueprintCallable, Category = ControlRigEditorExtensions)
	FControlRigGraphNodeContextMenuContext GetGraphNodeContextMenuContext();

private:
	TWeakObjectPtr<UControlRigBlueprint> ControlRigBlueprint;

	FControlRigRigHierarchyDragAndDropContext DragAndDropContext;

	FControlRigGraphNodeContextMenuContext GraphNodeContextMenuContext;
};