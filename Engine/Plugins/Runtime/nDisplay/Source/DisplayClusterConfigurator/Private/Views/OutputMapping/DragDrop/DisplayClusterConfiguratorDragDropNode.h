// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Input/DragAndDrop.h"

class SDisplayClusterConfiguratorBaseNode;

class FDisplayClusterConfiguratorDragDropNode
	: public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDisplayClusterConfiguratorDragDropNode, FDragDropOperation)

	static TSharedRef<FDisplayClusterConfiguratorDragDropNode> New(const TSharedRef<SDisplayClusterConfiguratorBaseNode>& InBaseNode);

	// FDragDropOperation interface
	virtual void OnDragged(const class FDragDropEvent& DragDropEvent) override;
	// End of FDragDropOperation interface

private:
	TWeakPtr<SDisplayClusterConfiguratorBaseNode> BaseNodePtr;
};
