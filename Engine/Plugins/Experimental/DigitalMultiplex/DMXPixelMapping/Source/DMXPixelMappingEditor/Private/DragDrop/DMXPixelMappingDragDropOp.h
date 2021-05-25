// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXPixelMappingComponentReference.h"

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"
#include "DragAndDrop/DecoratedDragDropOp.h"

class FDMXPixelMappingComponentTemplate;
class FDMXPixelMappingComponentReference;
class UDMXPixelMappingBaseComponent;
class UDMXPixelMappingOutputComponent;

/**
 * This drag drop operation allows Component templates from the palate to be dragged and dropped into the designer
 * or the Component hierarchy in order to spawn new Components.
 */
class FDMXPixelMappingDragDropOp 
	: public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDMXPixelMappingDragDropOp, FDecoratedDragDropOp)

	struct FDraggingComponentReference
	{
		FDMXPixelMappingComponentReference ComponentReference;

		FVector2D DraggedOffset = FVector2D::ZeroVector;
	};

	/** Constructs the drag drop operation */
	static TSharedRef<FDMXPixelMappingDragDropOp> New(const TSharedPtr<FDMXPixelMappingComponentTemplate>& InTemplate, UDMXPixelMappingBaseComponent* InParent = nullptr);
	static TSharedRef<FDMXPixelMappingDragDropOp> New(const TArray<FDMXPixelMappingDragDropOp::FDraggingComponentReference>& InComponentReferences);

	/** Returns the dragged component references */
	const TArray<FDMXPixelMappingDragDropOp::FDraggingComponentReference>& GetDraggingComponentReferences() const { return DraggingComponentReferences; }

	/** Sets the dragged component references */
	void SetDraggingComponentReferences(const TArray<FDMXPixelMappingDragDropOp::FDraggingComponentReference>& InDraggingComponentReferences) { DraggingComponentReferences = InDraggingComponentReferences; }

public:
	/** The template to create an instance */
	TSharedPtr<FDMXPixelMappingComponentTemplate> Template;

	TWeakObjectPtr<UDMXPixelMappingBaseComponent> Parent;

	TArray<FDMXPixelMappingDragDropOp::FDraggingComponentReference> DraggingComponentReferences;
};
