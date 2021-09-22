// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigContextMenuContext.h"

#include "ControlRigBlueprint.h"
#include "ControlRigEditor.h"
#include "Slate/Public/Framework/Application/SlateApplication.h"

FString FControlRigRigHierarchyToGraphDragAndDropContext::GetSectionTitle() const
{
	TArray<FString> ElementNameStrings;
	for (const FRigElementKey& Element: DraggedElementKeys)
	{
		ElementNameStrings.Add(Element.Name.ToString());
	}
	return FString::Join(ElementNameStrings, TEXT(","));
}

void UControlRigContextMenuContext::Init(TWeakObjectPtr<UControlRigBlueprint> InControlRigBlueprint, const FControlRigMenuSpecificContext& InMenuSpecificContext)
{
	ControlRigBlueprint = InControlRigBlueprint;
	MenuSpecificContext = InMenuSpecificContext;
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
	return MenuSpecificContext.RigHierarchyDragAndDropContext;
}

FControlRigGraphNodeContextMenuContext UControlRigContextMenuContext::GetGraphNodeContextMenuContext()
{
	return MenuSpecificContext.GraphNodeContextMenuContext;
}

FControlRigRigHierarchyToGraphDragAndDropContext UControlRigContextMenuContext::GetRigHierarchyToGraphDragAndDropContext()
{
	return MenuSpecificContext.RigHierarchyToGraphDragAndDropContext;
}