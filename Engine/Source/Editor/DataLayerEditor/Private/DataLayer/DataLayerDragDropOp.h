// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DragAndDrop/DecoratedDragDropOp.h"

class FDragDropEvent;
class AActor;
class UDataLayer;

typedef TPair< TWeakObjectPtr<AActor>, TWeakObjectPtr<UDataLayer>> FDataLayerActorMoveElement;

/** Drag/drop operation for dragging layers in the editor */
class FDataLayerDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDataLayerDragDropOp, FDecoratedDragDropOp)

	/** The labels of the layers being dragged */
	TArray<FName> DataLayerLabels;

	virtual void Construct() override;
};

/** Drag/drop operation for moving actor(s) in a data layer in the editor */
class FDataLayerActorMoveOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDataLayerActorMoveOp, FDecoratedDragDropOp)

	/** Actor that we are dragging */
	TArray<FDataLayerActorMoveElement> DataLayerActorMoveElements;

	virtual void Construct() override;
};