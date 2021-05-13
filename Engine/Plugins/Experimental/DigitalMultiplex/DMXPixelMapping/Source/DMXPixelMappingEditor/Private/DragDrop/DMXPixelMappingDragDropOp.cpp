// Copyright Epic Games, Inc. All Rights Reserved.

#include "DragDrop/DMXPixelMappingDragDropOp.h"

#include "Templates/DMXPixelMappingComponentTemplate.h"
#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "Components/DMXPixelMappingMatrixCellComponent.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"

TSharedRef<FDMXPixelMappingDragDropOp> FDMXPixelMappingDragDropOp::New(const TSharedPtr<FDMXPixelMappingComponentTemplate>& InTemplate, UDMXPixelMappingBaseComponent* InParent)
{
	TSharedRef<FDMXPixelMappingDragDropOp> Operation = MakeShared<FDMXPixelMappingDragDropOp>();

	Operation->Template = InTemplate;
	Operation->Parent = InParent;
	Operation->DefaultHoverText = InTemplate->Name;
	Operation->CurrentHoverText = InTemplate->Name;

	Operation->Construct();

	return Operation;
}

TSharedRef<FDMXPixelMappingDragDropOp> FDMXPixelMappingDragDropOp::New(const TArray<FDMXPixelMappingDragDropOp::FDraggingComponentReference>& InDraggingComponentReferences)
{
	TSharedRef<FDMXPixelMappingDragDropOp> Operation = MakeShared<FDMXPixelMappingDragDropOp>();

	Operation->DraggingComponentReferences = InDraggingComponentReferences;

	for (const FDMXPixelMappingDragDropOp::FDraggingComponentReference& DraggingComponentReference : InDraggingComponentReferences)
	{
		Operation->DefaultHoverText = FText::FromString(DraggingComponentReference.ComponentReference.GetComponent()->GetName());
		Operation->CurrentHoverText = FText::FromString(DraggingComponentReference.ComponentReference.GetComponent()->GetName());
		break;
	}

	Operation->Construct();

	return Operation;
}
