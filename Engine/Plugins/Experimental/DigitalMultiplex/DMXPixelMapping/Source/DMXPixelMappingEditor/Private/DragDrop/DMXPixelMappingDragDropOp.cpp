// Copyright Epic Games, Inc. All Rights Reserved.

#include "DragDrop/DMXPixelMappingDragDropOp.h"
#include "Templates/DMXPixelMappingComponentTemplate.h"
#include "Components/DMXPixelMappingBaseComponent.h"

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

TSharedRef<FDMXPixelMappingDragDropOp> FDMXPixelMappingDragDropOp::New(UDMXPixelMappingBaseComponent* InComponent)
{
	TSharedRef<FDMXPixelMappingDragDropOp> Operation = MakeShared<FDMXPixelMappingDragDropOp>();
	Operation->Component = InComponent;
	Operation->DefaultHoverText = FText::FromString(InComponent->GetName());
	Operation->CurrentHoverText = FText::FromString(InComponent->GetName());
	Operation->Construct();

	return Operation;
}
