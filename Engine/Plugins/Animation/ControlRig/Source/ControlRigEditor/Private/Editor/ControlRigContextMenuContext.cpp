// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigContextMenuContext.h"

#include "ControlRigBlueprint.h"
#include "ControlRigEditor.h"
#include "Slate/Public/Framework/Application/SlateApplication.h"

UControlRigBlueprint* UControlRigContextMenuContext::GetControlRigBlueprint() const
{
	if (ControlRigEditor.IsValid())
	{
		return ControlRigEditor.Pin()->GetControlRigBlueprint();
	}

	return nullptr;
}

UControlRig* UControlRigContextMenuContext::GetControlRig() const
{
	if (UControlRigBlueprint* Blueprint = GetControlRigBlueprint())
	{
		if (UControlRig* ControlRig = Cast<UControlRig>(Blueprint->GetObjectBeingDebugged()))
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

FControlRigRigHierarchyDragAndDropContext UControlRigContextMenuContext::GetDragAndDropContext()
{
	return DragAndDropContext;
}
