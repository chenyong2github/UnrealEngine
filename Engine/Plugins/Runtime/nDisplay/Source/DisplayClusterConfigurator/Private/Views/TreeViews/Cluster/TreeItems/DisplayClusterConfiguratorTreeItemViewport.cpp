// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorTreeItemViewport.h"

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfiguratorStyle.h"
#include "DisplayClusterProjectionStrings.h"
#include "DisplayClusterConfiguratorBlueprintEditor.h"
#include "DisplayClusterConfiguratorPropertyUtils.h"
#include "ClusterConfiguration/DisplayClusterConfiguratorClusterUtils.h"
#include "Views/DragDrop/DisplayClusterConfiguratorValidatedDragDropOp.h"
#include "Views/DragDrop/DisplayClusterConfiguratorViewportDragDropOp.h"
#include "Views/TreeViews/Cluster/DisplayClusterConfiguratorViewCluster.h"
#include "Views/TreeViews/Cluster/TreeItems/DisplayClusterConfiguratorTreeItemClusterNode.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorViewTree.h"

#include "ScopedTransaction.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "ISinglePropertyView.h"

#define LOCTEXT_NAMESPACE "FDisplayClusterConfiguratorTreeItemViewport"

FDisplayClusterConfiguratorTreeItemViewport::FDisplayClusterConfiguratorTreeItemViewport(const FName& InName,
	const TSharedRef<IDisplayClusterConfiguratorViewTree>& InViewTree,
	const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit,
	UObject* InObjectToEdit)
	: FDisplayClusterConfiguratorTreeItemCluster(InName, InViewTree, InToolkit, InObjectToEdit, "DisplayClusterConfigurator.TreeItems.Viewport", false)
{ }

void FDisplayClusterConfiguratorTreeItemViewport::SetVisible(bool bIsVisible)
{
	UDisplayClusterConfigurationViewport* Viewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();

	// Use SaveToTransactionBuffer to avoid marking the package as dirty
	SaveToTransactionBuffer(Viewport, false);

	const TSharedPtr<ISinglePropertyView> PropertyView = DisplayClusterConfiguratorPropertyUtils::GetPropertyView(
		Viewport, GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationViewport, bIsVisible));

	PropertyView->GetPropertyHandle()->SetValue(bIsVisible);
}

void FDisplayClusterConfiguratorTreeItemViewport::SetEnabled(bool bIsEnabled)
{
	UDisplayClusterConfigurationViewport* Viewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();

	// Use SaveToTransactionBuffer to avoid marking the package as dirty
	SaveToTransactionBuffer(Viewport, false);

	const TSharedPtr<ISinglePropertyView> PropertyView = DisplayClusterConfiguratorPropertyUtils::GetPropertyView(
		Viewport, GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationViewport, bIsEnabled));
	
	PropertyView->GetPropertyHandle()->SetValue(bIsEnabled);
}

void FDisplayClusterConfiguratorTreeItemViewport::OnSelection()
{
	FDisplayClusterConfiguratorTreeItemCluster::OnSelection();

	UDisplayClusterConfigurationViewport* Viewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();

	TArray<FString> AssociatedComponents;
	if (Viewport->ProjectionPolicy.Parameters.Contains(DisplayClusterProjectionStrings::cfg::simple::Screen))
	{
		AssociatedComponents.Add(Viewport->ProjectionPolicy.Parameters[DisplayClusterProjectionStrings::cfg::simple::Screen]);
	}
	else if (Viewport->ProjectionPolicy.Parameters.Contains(DisplayClusterProjectionStrings::cfg::mesh::Component))
	{
		AssociatedComponents.Add(Viewport->ProjectionPolicy.Parameters[DisplayClusterProjectionStrings::cfg::mesh::Component]);
	}
	else if (Viewport->ProjectionPolicy.Parameters.Contains(DisplayClusterProjectionStrings::cfg::camera::Component))
	{
		AssociatedComponents.Add(Viewport->ProjectionPolicy.Parameters[DisplayClusterProjectionStrings::cfg::camera::Component]);
	}

	if (AssociatedComponents.Num())
	{
		ToolkitPtr.Pin()->SelectAncillaryComponents(AssociatedComponents);
	}
}

TSharedRef<SWidget> FDisplayClusterConfiguratorTreeItemViewport::GenerateWidgetForColumn(const FName& ColumnName, TSharedPtr<ITableRow> TableRow, const TAttribute<FText>& FilterText, FIsSelected InIsSelected)
{
	if (ColumnName == FDisplayClusterConfiguratorViewCluster::Columns::Host)
	{
		return SNew(SImage)
			.ColorAndOpacity(this, &FDisplayClusterConfiguratorTreeItemViewport::GetHostColor)
			.OnMouseButtonDown(this, &FDisplayClusterConfiguratorTreeItemViewport::OnHostClicked)
			.Image(FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Node.Body"))
			.Cursor(EMouseCursor::Hand);
	}
	else if (ColumnName == FDisplayClusterConfiguratorViewCluster::Columns::Visible)
	{
		return SAssignNew(VisibilityButton, SButton)
			.ContentPadding(0)
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.ToolTipText(LOCTEXT("VisibilityButton_Tooltip", "Hides or shows this viewport in the Output Mapping editor"))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked(this, &FDisplayClusterConfiguratorTreeItemViewport::OnVisibilityButtonClicked)
			.Content()
			[
				SNew(SImage)
				.Image(this, &FDisplayClusterConfiguratorTreeItemViewport::GetVisibilityButtonBrush)
			];
	}
	else if (ColumnName == FDisplayClusterConfiguratorViewCluster::Columns::Enabled)
	{
		return SAssignNew(EnabledButton, SButton)
			.ContentPadding(0)
			.ButtonStyle(FEditorStyle::Get(), "NoBorder")
			.ToolTipText(LOCTEXT("EnabledButton_Tooltip", "Enables or disables this viewport in the Output Mapping editor"))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked(this, &FDisplayClusterConfiguratorTreeItemViewport::OnEnabledButtonClicked)
			.Content()
			[
				SNew(SImage)
				.Image(this, &FDisplayClusterConfiguratorTreeItemViewport::GetEnabledButtonBrush)
			];
	}

	return FDisplayClusterConfiguratorTreeItemCluster::GenerateWidgetForColumn(ColumnName, TableRow, FilterText, InIsSelected);
}

void FDisplayClusterConfiguratorTreeItemViewport::DeleteItem() const
{
	UDisplayClusterConfigurationViewport* Viewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();
	FDisplayClusterConfiguratorClusterUtils::RemoveViewportFromClusterNode(Viewport);
}

FReply FDisplayClusterConfiguratorTreeItemViewport::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TSharedRef<IDisplayClusterConfiguratorViewTree> ViewTree = GetConfiguratorTree();
	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
	{
		TArray<TSharedPtr<IDisplayClusterConfiguratorTreeItem>> SelectedItems = ViewTree->GetSelectedItems();

		TSharedPtr<FDisplayClusterConfiguratorTreeItemViewport> This = SharedThis(this);
		if (!SelectedItems.Contains(This))
		{
			SelectedItems.Insert(This, 0);
		}

		TArray<UObject*> SelectedObjects;
		for (TSharedPtr<IDisplayClusterConfiguratorTreeItem> SelectedItem : SelectedItems)
		{
			SelectedObjects.Add(SelectedItem->GetObject());
		}

		TSharedPtr<FDragDropOperation> DragDropOp = FDisplayClusterConfiguratorClusterUtils::MakeDragDropOperation(SelectedObjects);

		if (DragDropOp.IsValid())
		{
			return FReply::Handled().BeginDragDrop(DragDropOp.ToSharedRef());
		}
		else
		{
			return FReply::Unhandled();
		}
	}

	return FReply::Unhandled();
}

void FDisplayClusterConfiguratorTreeItemViewport::HandleDragEnter(const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FDisplayClusterConfiguratorViewportDragDropOp> ViewportDragDropOp = DragDropEvent.GetOperationAs<FDisplayClusterConfiguratorViewportDragDropOp>();
	if (ViewportDragDropOp.IsValid())
	{
		// In the case of a viewport drag drop, we are attempting to drop the dragged viewports as siblings of this viewport. We can pass the drop event to this
		// viewport's parent cluster node tree item to handle.
		TSharedPtr<IDisplayClusterConfiguratorTreeItem> ParentItem = Parent.Pin();
		if (ParentItem.IsValid())
		{
			ParentItem->HandleDragEnter(DragDropEvent);
			return;
		}
	}

	FDisplayClusterConfiguratorTreeItemCluster::HandleDragEnter(DragDropEvent);
}

void FDisplayClusterConfiguratorTreeItemViewport::HandleDragLeave(const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FDisplayClusterConfiguratorValidatedDragDropOp> ValidatedDragDropOp = DragDropEvent.GetOperationAs<FDisplayClusterConfiguratorValidatedDragDropOp>();
	if (ValidatedDragDropOp.IsValid())
	{
		// Use an opt-in policy for drag-drop operations, always marking it as invalid until another widget marks it as a valid drop operation
		ValidatedDragDropOp->SetDropAsInvalid();
		return;
	}

	FDisplayClusterConfiguratorTreeItemCluster::HandleDragLeave(DragDropEvent);
}

TOptional<EItemDropZone> FDisplayClusterConfiguratorTreeItemViewport::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<IDisplayClusterConfiguratorTreeItem> TargetItem)
{
	TSharedPtr<FDisplayClusterConfiguratorViewportDragDropOp> ViewportDragDropOp = DragDropEvent.GetOperationAs<FDisplayClusterConfiguratorViewportDragDropOp>();
	if (ViewportDragDropOp.IsValid())
	{
		if (ViewportDragDropOp->CanBeDropped())
		{
			return EItemDropZone::BelowItem;
		}
		else
		{
			return TOptional<EItemDropZone>();
		}
	}

	return FDisplayClusterConfiguratorTreeItemCluster::HandleCanAcceptDrop(DragDropEvent, DropZone, TargetItem);
}

FReply FDisplayClusterConfiguratorTreeItemViewport::HandleAcceptDrop(const FDragDropEvent& DragDropEvent, TSharedPtr<IDisplayClusterConfiguratorTreeItem> TargetItem)
{
	TSharedPtr<FDisplayClusterConfiguratorViewportDragDropOp> ViewportDragDropOp = DragDropEvent.GetOperationAs<FDisplayClusterConfiguratorViewportDragDropOp>();
	if (ViewportDragDropOp.IsValid())
	{
		// In the case of a viewport drag drop, we are attempting to drop the dragged viewports as siblings of this viewport. We can pass the drop event to this
		// viewport's parent cluster node tree item to handle the dropping.
		TSharedPtr<IDisplayClusterConfiguratorTreeItem> ParentItem = Parent.Pin();
		if (ParentItem.IsValid())
		{
			return ParentItem->HandleAcceptDrop(DragDropEvent, TargetItem);
		}
	}

	return FDisplayClusterConfiguratorTreeItemCluster::HandleAcceptDrop(DragDropEvent, TargetItem);
}

void FDisplayClusterConfiguratorTreeItemViewport::OnDisplayNameCommitted(const FText& NewText, ETextCommit::Type CommitInfo)
{
	// A viewport's "display name" is simply the key that it is stored under in its parent cluster node. In order to change it,
	// the viewport will need to be removed from the TMap and re-added under the new name/key.
	UDisplayClusterConfigurationViewport* Viewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();

	FScopedTransaction Transaction(NSLOCTEXT("FDisplayClusterConfiguratorViewCluster", "RenameClusterNode", "Rename Cluster Node"));
	FString NewName = NewText.ToString();

	if (FDisplayClusterConfiguratorClusterUtils::RenameViewport(Viewport, NewName))
	{
		Name = *NewName;

		Viewport->MarkPackageDirty();
		ToolkitPtr.Pin()->ClusterChanged();
	}
	else
	{
		Transaction.Cancel();
	}
}

FSlateColor FDisplayClusterConfiguratorTreeItemViewport::GetHostColor() const
{
	TSharedPtr<IDisplayClusterConfiguratorTreeItem> ParentItem = Parent.Pin();
	UDisplayClusterConfigurationHostDisplayData* HostDisplayData = nullptr;
	
	if (ParentItem.IsValid())
	{
		FDisplayClusterConfiguratorTreeItemClusterNode* ParentClusterNodeItem = StaticCast<FDisplayClusterConfiguratorTreeItemClusterNode*>(ParentItem.Get());
		HostDisplayData = ParentClusterNodeItem->GetHostDisplayData();
	}

	if (!HostDisplayData)
	{
		return FLinearColor::Transparent;
	}

	if (ToolkitPtr.Pin()->IsObjectSelected(HostDisplayData))
	{
		return FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Color.Selected");
	}
	else
	{
		return HostDisplayData->Color;
	}
}

FReply FDisplayClusterConfiguratorTreeItemViewport::OnHostClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TSharedPtr<IDisplayClusterConfiguratorTreeItem> ParentItem = GetParent();

	if (ParentItem.IsValid())
	{
		if (FDisplayClusterConfiguratorTreeItemClusterNode* ParentClusterNodeItem = StaticCast<FDisplayClusterConfiguratorTreeItemClusterNode*>(ParentItem.Get()))
		{
			ParentClusterNodeItem->SelectHostDisplayData();
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

const FSlateBrush* FDisplayClusterConfiguratorTreeItemViewport::GetVisibilityButtonBrush() const
{
	UDisplayClusterConfigurationViewport* Viewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();

	if (Viewport->bIsVisible)
	{
		return VisibilityButton->IsHovered() ? FEditorStyle::GetBrush(TEXT("Level.VisibleHighlightIcon16x")) : FEditorStyle::GetBrush(TEXT("Level.VisibleIcon16x"));
	}
	else
	{
		return VisibilityButton->IsHovered() ? FEditorStyle::GetBrush(TEXT("Level.NotVisibleHighlightIcon16x")) : FEditorStyle::GetBrush(TEXT("Level.NotVisibleIcon16x"));
	}
}

FReply FDisplayClusterConfiguratorTreeItemViewport::OnVisibilityButtonClicked()
{
	const FScopedTransaction Transaction(LOCTEXT("ToggleViewportVisibility", "Set Viewport Visibility"));

	UDisplayClusterConfigurationViewport* Viewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();

	// Use SaveToTransactionBuffer to avoid marking the package as dirty
	SaveToTransactionBuffer(Viewport, false);

	const TSharedPtr<ISinglePropertyView> PropertyView = DisplayClusterConfiguratorPropertyUtils::GetPropertyView(
		Viewport, GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationViewport, bIsVisible));

	PropertyView->GetPropertyHandle()->SetValue(!Viewport->bIsVisible);
	return FReply::Handled();
}

const FSlateBrush* FDisplayClusterConfiguratorTreeItemViewport::GetEnabledButtonBrush() const
{
	UDisplayClusterConfigurationViewport* Viewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();

	if (Viewport->bIsEnabled)
	{
		return EnabledButton->IsHovered() ? FEditorStyle::GetBrush(TEXT("Level.UnlockedHighlightIcon16x")) : FEditorStyle::GetBrush(TEXT("Level.UnlockedIcon16x"));
	}
	else
	{
		return EnabledButton->IsHovered() ? FEditorStyle::GetBrush(TEXT("Level.LockedHighlightIcon16x")) : FEditorStyle::GetBrush(TEXT("Level.LockedIcon16x"));
	}
}

FReply FDisplayClusterConfiguratorTreeItemViewport::OnEnabledButtonClicked()
{
	const FScopedTransaction Transaction(LOCTEXT("ToggleViewportEnabled", "Set Viewport Enabled"));

	UDisplayClusterConfigurationViewport* Viewport = GetObjectChecked<UDisplayClusterConfigurationViewport>();

	// Use SaveToTransactionBuffer to avoid marking the package as dirty
	SaveToTransactionBuffer(Viewport, false);

	const TSharedPtr<ISinglePropertyView> PropertyView = DisplayClusterConfiguratorPropertyUtils::GetPropertyView(
		Viewport, GET_MEMBER_NAME_CHECKED(UDisplayClusterConfigurationViewport, bIsEnabled));

	PropertyView->GetPropertyHandle()->SetValue(!Viewport->bIsEnabled);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE 