// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorBaseNode.h"

#include "DisplayClusterConfiguratorStyle.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorViewTree.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeItem.h"
#include "Views/OutputMapping/DragDrop/DisplayClusterConfiguratorDragDropNode.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorBaseNode.h"

#include "Widgets/Images/SImage.h"
#include "SGraphPanel.h"

void SDisplayClusterConfiguratorBaseNode::Construct(const FArguments& InArgs, UDisplayClusterConfiguratorBaseNode* InBaseNode, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{
	SGraphNode::Construct();

	ToolkitPtr = InToolkit;

	GraphNode = InBaseNode;
	check(GraphNode);

	SNodePanel::SNode::FNodeSet NodeFilter;
	SetCursor(EMouseCursor::CardinalCross);

	SetIsEditable(TAttribute<bool>());

	NodeSlot = nullptr;
	InNodeVisibile = true;

	// Register delegates
	TSharedRef<IDisplayClusterConfiguratorViewTree> ViewCluster = InToolkit->GetViewCluster();
	ViewCluster->RegisterOnSelectedItemSet(IDisplayClusterConfiguratorViewTree::FOnSelectedItemSetDelegate::CreateSP(this, &SDisplayClusterConfiguratorBaseNode::OnSelectedItemSet));
	ViewCluster->RegisterOnSelectedItemCleared(IDisplayClusterConfiguratorViewTree::FOnSelectedItemClearedDelegate::CreateSP(this, &SDisplayClusterConfiguratorBaseNode::OnSelectedItemCleared));
}

void SDisplayClusterConfiguratorBaseNode::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();

	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	// The rest of widgets should be created by child classes
}

bool SDisplayClusterConfiguratorBaseNode::SupportsKeyboardFocus() const
{
	return true;
}

FReply SDisplayClusterConfiguratorBaseNode::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	TSharedPtr<SGraphPanel> GraphPanel = GetOwnerPanel();
	static const float StandardMovePixelsStep = 1.f;

	// Apply changes only if node selected
	if (GraphPanel->SelectionManager.SelectedNodes.Contains(GraphNode))
	{
		FVector2D Offset = FVector2D::ZeroVector;
		
		if (InKeyEvent.GetKey() == EKeys::Left)
		{
			Offset.X = -StandardMovePixelsStep;
		}
		else if (InKeyEvent.GetKey() == EKeys::Right)
		{
			Offset.X = StandardMovePixelsStep;
		}
		else if (InKeyEvent.GetKey() == EKeys::Up)
		{
			Offset.Y = -StandardMovePixelsStep;
		}
		else if (InKeyEvent.GetKey() == EKeys::Down)
		{
			Offset.Y = StandardMovePixelsStep;
		}

		if (!Offset.IsZero())
		{
			SetNodePositionOffset(Offset / GraphPanel->GetZoomAmount());

			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

FReply SDisplayClusterConfiguratorBaseNode::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		ExecuteMouseButtonDown(MouseEvent);
		
		return FReply::Handled().DetectDrag(AsShared(), EKeys::LeftMouseButton);
	}

	return FReply::Unhandled();
}

FReply SDisplayClusterConfiguratorBaseNode::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// Release mouse capture
		return FReply::Handled().ReleaseMouseCapture();
	}

	return FReply::Unhandled();
}

FReply SDisplayClusterConfiguratorBaseNode::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Fire DragDropNode
	return FReply::Handled().BeginDragDrop(FDisplayClusterConfiguratorDragDropNode::New(SharedThis(this)));
}

FReply SDisplayClusterConfiguratorBaseNode::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// Back cursor to normal
	SetCursor(EMouseCursor::CardinalCross);

	TSharedPtr<FDisplayClusterConfiguratorDragDropNode> DragOperation = DragDropEvent.GetOperationAs<FDisplayClusterConfiguratorDragDropNode>();
	if (DragOperation.IsValid())
	{
		return FReply::Handled().EndDragDrop();
	}

	return SGraphNode::OnDrop(MyGeometry, DragDropEvent);
}

FCursorReply SDisplayClusterConfiguratorBaseNode::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	TOptional<EMouseCursor::Type> TheCursor = GetCursor();
	return (TheCursor.IsSet())
		? FCursorReply::Cursor(TheCursor.GetValue())
		: FCursorReply::Unhandled();
}

const FSlateBrush* SDisplayClusterConfiguratorBaseNode::GetShadowBrush(bool bSelected) const
{
	return FEditorStyle::GetNoBrush();
}

bool SDisplayClusterConfiguratorBaseNode::ShouldAllowCulling() const
{
	return false;
}

void SDisplayClusterConfiguratorBaseNode::ExecuteMouseButtonDown(const FPointerEvent& MouseEvent)
{
	GetOwnerPanel()->SelectionManager.ClickedOnNode(GraphNode, MouseEvent);
}

TSharedRef<SWidget> SDisplayClusterConfiguratorBaseNode::CreateBackground(const TAttribute<FSlateColor>& InColorAndOpacity)
{
	return SNew(SOverlay)
		+SOverlay::Slot()
		.VAlign(VAlign_Fill)
		.HAlign(HAlign_Fill)
		.Padding(0.f)
		[
			SNew(SImage)
			.ColorAndOpacity(InColorAndOpacity)
			.Image(FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Node.Body"))
		];
}

EVisibility SDisplayClusterConfiguratorBaseNode::GetSelectionVisibility() const
{
	if (GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode))
	{
		return EVisibility::Visible;
	}

	return EVisibility::Hidden;
}

bool SDisplayClusterConfiguratorBaseNode::IsNodeVisible() const
{
	return InNodeVisibile;
}

EVisibility SDisplayClusterConfiguratorBaseNode::GetNodeVisibility() const
{
	if (InNodeVisibile)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Hidden;
}

void SDisplayClusterConfiguratorBaseNode::OnSelectedItemCleared()
{
	InNodeVisibile = true;
}

bool SDisplayClusterConfiguratorBaseNode::OnNodeDragged(const FVector2D& DragScreenSpacePosition, const FVector2D& ScreenSpaceDelta)
{
	TSharedPtr<SGraphPanel> GraphPanel = GetOwnerPanel();
	check(GraphPanel.IsValid());

	const FGeometry& PanelGeometry = GraphPanel->GetTickSpaceGeometry();
	FVector2D PanelLocalSize = PanelGeometry.GetLocalSize();
	FVector2D CursorLocalPosition = PanelGeometry.AbsoluteToLocal(DragScreenSpacePosition);

	// Set node new position based on offset
	SetNodePositionOffset(ScreenSpaceDelta / GraphPanel->GetZoomAmount());

	// If the pointer is leaving panel's window to return false to change the cursor
	if (CursorLocalPosition.X < 0 || 
		CursorLocalPosition.X > PanelLocalSize.X || 
		CursorLocalPosition.Y < 0 || 
		CursorLocalPosition.Y > PanelLocalSize.Y)
	{
		return false;
	}

	return true;
}
