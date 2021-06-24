// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigContextMenuContext.h"

#include "ControlRigBlueprint.h"
#include "ControlRigEditor.h"
#include "Slate/Public/Framework/Application/SlateApplication.h"

void UControlRigContextMenuContext::Init(TWeakObjectPtr<UControlRigBlueprint> InControlRigBlueprint, const FControlRigRigHierarchyDragAndDropContext& InDragAndDropContext, const FControlRigGraphNodeContextMenuContext& InGraphNodeContext)
{
	ControlRigBlueprint = InControlRigBlueprint;
	DragAndDropContext = InDragAndDropContext;
	GraphNodeContextMenuContext = InGraphNodeContext;
}

UControlRigBlueprint* UControlRigContextMenuContext::GetControlRigBlueprint() const
{
	return ControlRigBlueprint.Get();
}

UControlRig* UControlRigContextMenuContext::GetControlRig() const
{
	if (UControlRigBlueprint* RigBlueprint = GetControlRigBlueprint())
	{
		if (UControlRig* ControlRig = Cast<UControlRig>(RigBlueprint->GetObjectBeingDebugged()))
		{
			return ControlRig;
		}
	}
	return nullptr;
}

bool UControlRigContextMenuContext::IsAltDown() const
{
	return FSlateApplication::Get().GetModifierKeys().IsAltDown();
}

FControlRigRigHierarchyDragAndDropContext UControlRigContextMenuContext::GetRigHierarchyDragAndDropContext()
{
	return DragAndDropContext;
}

FControlRigGraphNodeContextMenuContext UControlRigContextMenuContext::GetGraphNodeContextMenuContext()
{
	return GraphNodeContextMenuContext;
}
