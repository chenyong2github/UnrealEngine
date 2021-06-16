// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rigs/RigHierarchyDefines.h"

#include "ControlRigContextMenuContext.generated.h"

class UControlRig;
class UControlRigBlueprint;
class FControlRigEditor;

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

UCLASS(BlueprintType)
class UControlRigContextMenuContext : public UObject
{
	GENERATED_BODY()

public:
	/**
	 *	Initialize the Context
	 * @param InControlRigEditor 		The Control Rig editor that hosts the menu
	 * @param InDragAndDropContext 		Optional, only used for the menu that shows up after a drag & drop action
	*/
	void Init(TWeakPtr<FControlRigEditor> InControlRigEditor, FControlRigRigHierarchyDragAndDropContext InDragAndDropContext = FControlRigRigHierarchyDragAndDropContext())
	{
		ControlRigEditor = InControlRigEditor;
		DragAndDropContext = InDragAndDropContext;
	}
	
	/** Get the control rig blueprint that we are editing */
	UFUNCTION(BlueprintCallable, Category = ControlRigEditorExtensions)
    UControlRigBlueprint* GetControlRigBlueprint() const;
	
	/** Get the active control rig instance in the viewport */
	UFUNCTION(BlueprintCallable, Category = ControlRigEditorExtensions)
    UControlRig* GetControlRig() const;

	/** Returns true if either alt key is down */
	UFUNCTION(BlueprintCallable, Category = ControlRigEditorExtensions)
	bool IsAltDown() const;
	
	/** Returns the source and target element keys of a drag & drop action */
	UFUNCTION(BlueprintCallable, Category = ControlRigEditorExtensions)
	FControlRigRigHierarchyDragAndDropContext GetDragAndDropContext();

private:
	/** Our owning control rig editor */
	TWeakPtr<FControlRigEditor> ControlRigEditor;

	FControlRigRigHierarchyDragAndDropContext DragAndDropContext;
};