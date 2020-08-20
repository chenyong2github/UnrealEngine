// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"
#include "DragAndDrop/DecoratedDragDropOp.h"

class FDMXPixelMappingComponentTemplate;
class UDMXPixelMappingBaseComponent;

/**
 * This drag drop operation allows Component templates from the palate to be dragged and dropped into the designer
 * or the Component hierarchy in order to spawn new Components.
 */
class FDMXPixelMappingDragDropOp 
	: public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDMXPixelMappingDragDropOp, FDecoratedDragDropOp)

	/** The template to create an instance */
	TSharedPtr<FDMXPixelMappingComponentTemplate> Template;

	TWeakObjectPtr<UDMXPixelMappingBaseComponent> Parent;

	TWeakObjectPtr<UDMXPixelMappingBaseComponent> Component;

	/** Constructs the drag drop operation */
	static TSharedRef<FDMXPixelMappingDragDropOp> New(const TSharedPtr<FDMXPixelMappingComponentTemplate>& InTemplate, UDMXPixelMappingBaseComponent* InParent = nullptr);
	static TSharedRef<FDMXPixelMappingDragDropOp> New(UDMXPixelMappingBaseComponent* InComponent = nullptr);
};
