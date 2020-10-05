// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/Scene/TreeItems/DisplayClusterConfiguratorTreeItemScene.h"

#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorViewTree.h"
#include "Views/TreeViews/Scene/DisplayClusterConfiguratorViewSceneDragDrop.h"
#include "Views/TreeViews/SDisplayClusterConfiguratorTreeItemRow.h"

#define LOCTEXT_NAMESPACE "FDisplayClusterConfiguratorTreeItemScene"

FDisplayClusterConfiguratorTreeItemScene::FDisplayClusterConfiguratorTreeItemScene(const FName& InName,
	const TSharedRef<IDisplayClusterConfiguratorViewTree>& InViewTree,
	const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit,
	UObject* InObjectToEdit,
	FString InIconStyle,
	bool InbRoot)
	: FDisplayClusterConfiguratorTreeItem(InViewTree, InToolkit, InObjectToEdit, InbRoot)
	, Name(InName)
	, IconStyle(InIconStyle)
{}

void FDisplayClusterConfiguratorTreeItemScene::OnMouseEnter()
{
	ViewTreePtr.Pin()->SetHoveredItem(SharedThis(this));
}

void FDisplayClusterConfiguratorTreeItemScene::OnMouseLeave()
{
	ViewTreePtr.Pin()->ClearHoveredItem();
}

bool FDisplayClusterConfiguratorTreeItemScene::IsHovered() const
{
	if (TSharedPtr<IDisplayClusterConfiguratorTreeItem> HoveredItem = ViewTreePtr.Pin()->GetHoveredItem())
	{
		return HoveredItem.Get() == this;
	}

	return false;
}

FReply FDisplayClusterConfiguratorTreeItemScene::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	TSharedPtr<IDisplayClusterConfiguratorViewTree> ViewTree = ViewTreePtr.Pin();
	check(ViewTree.IsValid());
	
	if (!IsRoot() && ViewTree->GetIsEnabled())
	{
		return FReply::Handled().BeginDragDrop(FDisplayClusterConfiguratorViewSceneDragDrop::New(ToolkitPtr.Pin().ToSharedRef(), ViewTreePtr.Pin().ToSharedRef(), SharedThis(this)));
	}

	return FReply::Unhandled();
}

void FDisplayClusterConfiguratorTreeItemScene::HandleDragEnter(const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FDisplayClusterConfiguratorViewSceneDragDrop> DragRowOp = DragDropEvent.GetOperationAs<FDisplayClusterConfiguratorViewSceneDragDrop>();
	if (DragRowOp.IsValid())
	{
		TSharedPtr<FDisplayClusterConfiguratorTreeItemScene> DragItemScene = DragRowOp->GetTreeItemScene();
		check(DragItemScene.IsValid());

		UDisplayClusterConfigurationSceneComponent* DragComponent = Cast<UDisplayClusterConfigurationSceneComponent>(DragItemScene->GetObject());
		bool bCanDrop = false;
		FText Message;

		if (IsRoot())
		{
			Message = LOCTEXT("DropActionToolTip_Error_CannotReparentRootComponent", "The root cannot be replaced.");
		}
		else if (DragItemScene->GetRowItemName() == Name)
		{
			Message = FText::Format(LOCTEXT("DropActionToolTip_Error_CannotAttachToSelf", "Cannot attach {0} to itself."), FText::FromName(DragItemScene->GetRowItemName()));
		}
		else if (IsChildOfRecursive(DragItemScene.ToSharedRef()))
		{
			Message = FText::Format(LOCTEXT("DropActionToolTip_Error_CannotAttachToChild", "Cannot attach {0} to one of its children."), FText::FromName(DragItemScene->GetRowItemName()));
		}
		else
		{
			bCanDrop = true;

			if (DragComponent->ParentId.Equals(Name.ToString()))
			{
				Message = FText::Format(LOCTEXT("DropActionToolTip_DetachFromThisComponent", "Drop here to detach the selected components from {0}."), FText::FromName(Name));
			}
			else
			{
				Message = FText::Format(LOCTEXT("DropActionToolTip_AttachToThisComponent", "Drop here to attach {0} to {1}."), FText::FromName(DragItemScene->GetRowItemName()), FText::FromName(Name));
			}
		}

		DragRowOp->SetCanDrop(bCanDrop);

		const FSlateBrush* StatusSymbol = bCanDrop
			? FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.OK"))
			: FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));

		DragRowOp->SetSimpleFeedbackMessage(StatusSymbol, FLinearColor::White, Message);
	}
}

FReply FDisplayClusterConfiguratorTreeItemScene::HandleAcceptDrop(const FDragDropEvent& DragDropEvent, TSharedPtr<IDisplayClusterConfiguratorTreeItem> TargetItem)
{
	TSharedPtr<FDisplayClusterConfiguratorViewSceneDragDrop> DragRowOp = DragDropEvent.GetOperationAs<FDisplayClusterConfiguratorViewSceneDragDrop>();
	if (DragRowOp.IsValid())
	{
		if (DragRowOp->CanDrop())
		{
			TSharedPtr<FDisplayClusterConfiguratorTreeItemScene> DragItemScene = DragRowOp->GetTreeItemScene();
			check(DragItemScene.IsValid());

			UDisplayClusterConfigurationSceneComponent* DragComponent = Cast<UDisplayClusterConfigurationSceneComponent>(DragItemScene->GetObject());
			UDisplayClusterConfigurationSceneComponent* ItemComponent = Cast<UDisplayClusterConfigurationSceneComponent>(GetObject());

			FString ParentRowString;
			if (DragComponent != nullptr)
			{
				if (DragComponent->ParentId.Equals(Name.ToString()))
				{
					ParentRowString = ItemComponent->ParentId;
				}
				else if (ItemComponent->ParentId.IsEmpty() && !DragComponent->ParentId.IsEmpty())
				{
					ParentRowString = "";
				}
				else
				{
					ParentRowString = Name.ToString();
				}

				DragComponent->ParentId = ParentRowString;

				// Refresh tree
				ViewTreePtr.Pin()->RebuildTree();
			}
		}
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE

