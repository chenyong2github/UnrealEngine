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

	/** Constructs the drag drop operation */
	static TSharedRef<FDMXPixelMappingDragDropOp> New(const TSharedPtr<FDMXPixelMappingComponentTemplate>& InTemplate, UDMXPixelMappingBaseComponent* InParent = nullptr);
	static TSharedRef<FDMXPixelMappingDragDropOp> New(const TSet<FDMXPixelMappingComponentReference>& InComponentReferences);

	void UpdateDragOffset(const FVector2D& DragStartScreenspacePosition);

	void SetComponentReferences(const TSet<FDMXPixelMappingComponentReference>& InComponentReferences);

	/** Gets the offset of the drag drop op from the mouse pos (context menu location) */
	FVector2D GetDragOffset() const;

	/** Returns an output component or nullptr if it's not an output component drag drop op */
	UDMXPixelMappingOutputComponent* TryGetOutputComponent() const;

	/** Returns an base component or nullptr if it's not an output component drag drop op */
	UDMXPixelMappingBaseComponent* TryGetBaseComponent() const;

protected:
	/** Gets an arranged widget from the dragged component */
	FArrangedWidget GetArrangedWidgetFromComponent() const;

	/** Gets an arranged widget from a widget */
	bool GetArrangedWidget(TSharedRef<SWidget> Widget, FArrangedWidget& ArrangedWidget) const;

public:
	/** The template to create an instance */
	TSharedPtr<FDMXPixelMappingComponentTemplate> Template;

	TWeakObjectPtr<UDMXPixelMappingBaseComponent> Parent;

	TSet<FDMXPixelMappingComponentReference> ComponentReferences;

private:
	FVector2D DragOffset;
};
