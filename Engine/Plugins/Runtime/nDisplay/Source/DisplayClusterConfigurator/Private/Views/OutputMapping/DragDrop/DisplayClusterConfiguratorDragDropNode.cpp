// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/DragDrop/DisplayClusterConfiguratorDragDropNode.h"

#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorBaseNode.h"

TSharedRef<FDisplayClusterConfiguratorDragDropNode> FDisplayClusterConfiguratorDragDropNode::New(const TSharedRef<SDisplayClusterConfiguratorBaseNode>& InBaseNode)
{
	TSharedPtr<FDisplayClusterConfiguratorDragDropNode> DragDropNode = MakeShared<FDisplayClusterConfiguratorDragDropNode>();

	DragDropNode->BaseNodePtr = InBaseNode;

	return DragDropNode.ToSharedRef();
}

void FDisplayClusterConfiguratorDragDropNode::OnDragged(const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<SDisplayClusterConfiguratorBaseNode> BaseNode = BaseNodePtr.Pin();
	check(BaseNode.IsValid());

	const bool bNodeDragged = BaseNode->OnNodeDragged(DragDropEvent.GetScreenSpacePosition(), DragDropEvent.GetCursorDelta());

	MouseCursor = bNodeDragged ? EMouseCursor::GrabHand : EMouseCursor::SlashedCircle;

	FDragDropOperation::OnDragged(DragDropEvent);
}
