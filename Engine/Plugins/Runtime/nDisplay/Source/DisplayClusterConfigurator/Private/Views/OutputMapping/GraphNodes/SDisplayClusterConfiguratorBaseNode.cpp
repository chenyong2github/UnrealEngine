// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorBaseNode.h"

#include "DisplayClusterConfiguratorStyle.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorViewTree.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeItem.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorBaseNode.h"

#include "Widgets/Images/SImage.h"
#include "SGraphPanel.h"

void SDisplayClusterConfiguratorBaseNode::Construct(const FArguments& InArgs, UDisplayClusterConfiguratorBaseNode* InBaseNode, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{
	SGraphNode::Construct();

	ToolkitPtr = InToolkit;

	GraphNode = InBaseNode;
	check(GraphNode);

	SetCursor(TAttribute<TOptional<EMouseCursor::Type>>(this, &SDisplayClusterConfiguratorBaseNode::GetCursor));

	bIsObjectFocused = true;

	// Register delegates
	TSharedRef<IDisplayClusterConfiguratorViewTree> ViewCluster = InToolkit->GetViewCluster();
	ViewCluster->RegisterOnSelectedItemSet(IDisplayClusterConfiguratorViewTree::FOnSelectedItemSetDelegate::CreateSP(this, &SDisplayClusterConfiguratorBaseNode::OnSelectedItemSet));
	ViewCluster->RegisterOnSelectedItemCleared(IDisplayClusterConfiguratorViewTree::FOnSelectedItemClearedDelegate::CreateSP(this, &SDisplayClusterConfiguratorBaseNode::OnSelectedItemCleared));
}

FCursorReply SDisplayClusterConfiguratorBaseNode::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return SWidget::OnCursorQuery(MyGeometry, CursorEvent);
}

void SDisplayClusterConfiguratorBaseNode::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();

	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	// The rest of widgets should be created by child classes
}

const FSlateBrush* SDisplayClusterConfiguratorBaseNode::GetShadowBrush(bool bSelected) const
{
	return FEditorStyle::GetNoBrush();
}

bool SDisplayClusterConfiguratorBaseNode::CanBeSelected(const FVector2D& MousePositionInNode) const
{
	return IsNodeVisible();
}

bool SDisplayClusterConfiguratorBaseNode::ShouldAllowCulling() const
{
	return false;
}

int32 SDisplayClusterConfiguratorBaseNode::GetSortDepth() const
{
	int32 Depth = GetNodeLayerIndex() + ZIndex;

	// If this node is selected, add 1 to its depth to ensure it is drawn an top of other nodes in its layer.
	if (GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode))
	{
		Depth++;
	}

	return Depth;
}

void SDisplayClusterConfiguratorBaseNode::OnSelectedItemCleared()
{
	bIsObjectFocused = true;
}

bool SDisplayClusterConfiguratorBaseNode::IsNodeVisible() const
{
	return bIsObjectFocused;
}

FVector2D SDisplayClusterConfiguratorBaseNode::GetSize() const
{
	return FVector2D(GraphNode->NodeWidth, GraphNode->NodeHeight);
}

void SDisplayClusterConfiguratorBaseNode::ExecuteMouseButtonDown(const FPointerEvent& MouseEvent)
{
	GetOwnerPanel()->SelectionManager.ClickedOnNode(GraphNode, MouseEvent);
}

EVisibility SDisplayClusterConfiguratorBaseNode::GetNodeVisibility() const
{
	if (IsNodeVisible())
	{
		return EVisibility::Visible;
	}

	return EVisibility::Hidden;
}

EVisibility SDisplayClusterConfiguratorBaseNode::GetSelectionVisibility() const
{
	if (GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode))
	{
		return EVisibility::Visible;
	}

	return EVisibility::Hidden;
}

TOptional<EMouseCursor::Type> SDisplayClusterConfiguratorBaseNode::GetCursor() const
{
	if (IsNodeVisible())
	{
		return EMouseCursor::CardinalCross;
	}

	return EMouseCursor::Default;
}
