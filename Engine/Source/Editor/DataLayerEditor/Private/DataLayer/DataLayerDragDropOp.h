// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DragAndDrop/DecoratedDragDropOp.h"

class FDragDropEvent;

/** Drag/drop operation for dragging layers in the editor */
class FDataLayerDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDataLayerDragDropOp, FDecoratedDragDropOp)

	/** The labels of the layers being dragged */
	TArray<FName> DataLayerLabels;

	virtual void Construct() override;
};
