// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseWatchManagerDefaultMode.h"
#include "AnimationEditorUtils.h"
#include "Engine/PoseWatch.h"
#include "SGraphNode.h"
#include "SGraphPanel.h"
#include "IAnimationBlueprintEditor.h"
#include "PoseWatchManagerFolderTreeItem.h"
#include "PoseWatchManagerPoseWatchTreeItem.h"
#include "SPoseWatchManager.h"

#define LOCTEXT_NAMESPACE "PoseWatchDefaultMode"

/** Functor which can be used to get weak actor pointers from a selection */
struct FWeakPoseWatchSelector
{
	bool operator()(const TWeakPtr<IPoseWatchManagerTreeItem>& Item, TWeakObjectPtr<UPoseWatch>& DataOut) const;
};

FPoseWatchManagerDefaultMode::FPoseWatchManagerDefaultMode(SPoseWatchManager* InPoseWatchManager)
	: PoseWatchManager(InPoseWatchManager)
{
	Rebuild();
}

void FPoseWatchManagerDefaultMode::Rebuild()
{
	Hierarchy = MakeUnique<FPoseWatchManagerDefaultHierarchy>(this);
}

bool FPoseWatchManagerDefaultMode::ParseDragDrop(FPoseWatchManagerDragDropPayload& OutPayload, const FDragDropOperation& Operation) const
{
	if (Operation.IsOfType<FPoseWatchManagerDragDropOp>())
	{
		const auto& OutlinerOp = static_cast<const FPoseWatchManagerDragDropOp&>(Operation);
		if (const auto& PoseWatchOp = OutlinerOp.GetSubOp<FPoseWatchDragDropOp>())
		{
			OutPayload.DraggedItem = PoseWatchManager->GetTreeItem(PoseWatchOp->PoseWatch.Get());
		}
		if (const auto& FolderOp = OutlinerOp.GetSubOp<FPoseWatchFolderDragDropOp>())
		{
			OutPayload.DraggedItem = PoseWatchManager->GetTreeItem(FolderOp->PoseWatchFolder.Get());
		}
		return true;
	}

	return false;
}

FPoseWatchManagerDragValidationInfo FPoseWatchManagerDefaultMode::ValidateDrop(const IPoseWatchManagerTreeItem& DropTarget, const FPoseWatchManagerDragDropPayload& Payload) const
{
	check(Payload.DraggedItem.IsValid())
	TSharedPtr<IPoseWatchManagerTreeItem> PayloadItem = Payload.DraggedItem.Pin();
	
	// Support for removing the parent by dragging an item to the bottom of the tree
	if (!DropTarget.IsValid())
	{
		return FPoseWatchManagerDragValidationInfo(EPoseWatchManagerDropCompatibility::Compatible, LOCTEXT("MoveToRoot", "Move to root"));
	}

	// Pose watches cannot be a parent
	if (const FPoseWatchManagerPoseWatchTreeItem* PoseWatchDropTarget = DropTarget.CastTo<FPoseWatchManagerPoseWatchTreeItem>())
	{
		return FPoseWatchManagerDragValidationInfo(EPoseWatchManagerDropCompatibility::Incompatible, LOCTEXT("PoseWatchBadParent", "Pose Watches cannot be parents"));
	}

	// Folders can be parents as long as the payload is not already a parent of the drop target, otherwise there'll be cycles
	if (const FPoseWatchManagerFolderTreeItem* FolderItem = DropTarget.CastTo<FPoseWatchManagerFolderTreeItem>())
	{
		if (FolderItem->PoseWatchFolder.IsValid())
		{
			// Dropping a folder into a folder
			if (const FPoseWatchManagerFolderTreeItem* FolderPayloadItem = PayloadItem.Get()->CastTo<FPoseWatchManagerFolderTreeItem>())
			{
				if (FolderPayloadItem->PoseWatchFolder == FolderItem->PoseWatchFolder)
				{
					return FPoseWatchManagerDragValidationInfo(EPoseWatchManagerDropCompatibility::Incompatible, LOCTEXT("FolderInItself", "A folder cannot contain itself"));
				}

				if (FolderPayloadItem->PoseWatchFolder->IsIn(FolderItem->PoseWatchFolder.Get()))
				{
					FText ValidationText = FText::Format(LOCTEXT("MovePoseWatchIntoFolder", "This folder is already inside {0}"), FolderItem->PoseWatchFolder->GetLabel());
					return FPoseWatchManagerDragValidationInfo(EPoseWatchManagerDropCompatibility::Incompatible, ValidationText);
				}

				//if (FolderPayloadItem->PoseWatchFolder->IsDescendantOf(FolderItem->PoseWatchFolder.Get()))
				if (FolderItem->PoseWatchFolder->IsDescendantOf(FolderPayloadItem->PoseWatchFolder.Get()))
				{
					return FPoseWatchManagerDragValidationInfo(EPoseWatchManagerDropCompatibility::Incompatible, LOCTEXT("ParentFolderNotChildren", "Parent folders cannot be children"));
				}

				FText OutErrorMessage;
				FText Label = FolderPayloadItem->PoseWatchFolder->GetLabel();
				UPoseWatchFolder* ConflictingFolder = nullptr;

				if (FolderPayloadItem->PoseWatchFolder->IsFolderLabelUniqueInFolder(FolderPayloadItem->PoseWatchFolder->GetLabel(), FolderItem->PoseWatchFolder.Get()))
				{
					FText ValidationText = FText::Format(LOCTEXT("MovePoseWatchIntoFolder", "Move into {0}"), FolderItem->PoseWatchFolder->GetLabel());
					return FPoseWatchManagerDragValidationInfo(EPoseWatchManagerDropCompatibility::Compatible, ValidationText);
				}
				else
				{
					FText ValidationText = FText::Format(LOCTEXT("MovePoseWatchIntoFolder", "A folder with that name already exists within {0}"), FolderItem->PoseWatchFolder->GetLabel());
					return FPoseWatchManagerDragValidationInfo(EPoseWatchManagerDropCompatibility::Incompatible, ValidationText);
				}
			}

			// Dropping a pose watch into a folder
			if (const FPoseWatchManagerPoseWatchTreeItem* PoseWatchPayloadItem = PayloadItem.Get()->CastTo<FPoseWatchManagerPoseWatchTreeItem>())
			{
				if (PoseWatchPayloadItem->PoseWatch->IsIn(FolderItem->PoseWatchFolder.Get()))
				{
					FText ValidationText = FText::Format(LOCTEXT("MovePoseWatchIntoFolder", "This pose watch is already inside {0}"), FolderItem->PoseWatchFolder->GetLabel());
					return FPoseWatchManagerDragValidationInfo(EPoseWatchManagerDropCompatibility::Incompatible, ValidationText);
				}
				else
				{
					FText ValidationText = FText::Format(LOCTEXT("MovePoseWatchIntoFolder", "Move into {0}"), FolderItem->PoseWatchFolder->GetLabel());
					return FPoseWatchManagerDragValidationInfo(EPoseWatchManagerDropCompatibility::Compatible, ValidationText);
				}
			}
		}
		else
		{
			if (PayloadItem.Get()->IsAssignedFolder())
			{
				return FPoseWatchManagerDragValidationInfo(EPoseWatchManagerDropCompatibility::Compatible, LOCTEXT("MoveToRoot", "Move to root"));
			}
			else
			{
				return FPoseWatchManagerDragValidationInfo(EPoseWatchManagerDropCompatibility::Incompatible, LOCTEXT("FolderAlreadyInRoot", "This item is already in root"));
			}
		}
	}
	
	return FPoseWatchManagerDragValidationInfo(EPoseWatchManagerDropCompatibility::Incompatible, FText());
}

void FPoseWatchManagerDefaultMode::OnDrop(IPoseWatchManagerTreeItem& DropTarget, const FPoseWatchManagerDragDropPayload& Payload, const FPoseWatchManagerDragValidationInfo& ValidationInfo) const
{
	check(Payload.DraggedItem.IsValid());
	check(DropTarget.IsA<FPoseWatchManagerFolderTreeItem>());
	const FPoseWatchManagerFolderTreeItem* FolderDropTarget = DropTarget.CastTo<FPoseWatchManagerFolderTreeItem>();
	const TWeakPtr<IPoseWatchManagerTreeItem> PayloadItem = Payload.DraggedItem;

	if (const FPoseWatchManagerFolderTreeItem* ChildFolderItem = PayloadItem.Pin()->CastTo<FPoseWatchManagerFolderTreeItem>())
	{
		ChildFolderItem->PoseWatchFolder->MoveTo(FolderDropTarget->PoseWatchFolder.Get());
		return;
	}
	if (const FPoseWatchManagerPoseWatchTreeItem* ChildPoseWatchItem = PayloadItem.Pin()->CastTo<FPoseWatchManagerPoseWatchTreeItem>())
	{
		ChildPoseWatchItem->PoseWatch->MoveTo(FolderDropTarget->PoseWatchFolder.Get());
		return;
	}
	check(false);
}

TSharedPtr<FDragDropOperation> FPoseWatchManagerDefaultMode::CreateDragDropOperation(const TArray<FPoseWatchManagerTreeItemPtr>& InTreeItems) const
{
	check(InTreeItems.Num() == 1)
	FPoseWatchManagerTreeItemPtr TreeItem = InTreeItems[0];

	FPoseWatchManagerDragDropPayload DraggedObjects(TreeItem);

	TSharedPtr<FPoseWatchManagerDragDropOp> OutlinerOp = MakeShareable(new FPoseWatchManagerDragDropOp());

	if (FPoseWatchManagerPoseWatchTreeItem* PoseWatchTreeItem = TreeItem->CastTo<FPoseWatchManagerPoseWatchTreeItem>())
	{
		TSharedPtr<FPoseWatchDragDropOp> Operation = MakeShareable(new FPoseWatchDragDropOp);
		Operation->Init(TWeakObjectPtr<UPoseWatch>(PoseWatchTreeItem->PoseWatch));
		OutlinerOp->AddSubOp(Operation);
	}

	else if (FPoseWatchManagerFolderTreeItem* FolderTreeItem = TreeItem->CastTo<FPoseWatchManagerFolderTreeItem>())
	{
		TSharedPtr<FPoseWatchFolderDragDropOp> Operation = MakeShareable(new FPoseWatchFolderDragDropOp);
		Operation->Init(TWeakObjectPtr<UPoseWatchFolder>(FolderTreeItem->PoseWatchFolder));
		OutlinerOp->AddSubOp(Operation);
	}

	OutlinerOp->Construct();
	return OutlinerOp;
}

FReply FPoseWatchManagerDefaultMode::OnDragOverItem(const FDragDropEvent& Event, const IPoseWatchManagerTreeItem& Item) const
{
	return FReply::Handled();
}

TSharedPtr<SWidget> FPoseWatchManagerDefaultMode::CreateContextMenu()
{
	FPoseWatchManagerTreeItemPtr SelectedItem = PoseWatchManager->GetSelection();
	return SelectedItem ? SelectedItem->CreateContextMenu() : SNullWidget::NullWidget;
}

FReply FPoseWatchManagerDefaultMode::OnKeyDown(const FKeyEvent& InKeyEvent)
{
	const FPoseWatchManagerTreeItemPtr& Selection = PoseWatchManager->GetSelection();

	if (InKeyEvent.GetKey() == EKeys::F2)
	{
		if (Selection)
		{
			//FPoseWatchManagerTreeItemPtr ItemToRename = Selection.Get();

			if (Selection.IsValid())
			{
				PoseWatchManager->SetPendingRenameItem(Selection);
				PoseWatchManager->ScrollItemIntoView(Selection);
			}

			return FReply::Handled();
		}
	}

	else if (InKeyEvent.GetKey() == EKeys::F5)
	{
		PoseWatchManager->FullRefresh();
		return FReply::Handled();
	}

	else if (InKeyEvent.GetKey() == EKeys::Delete || InKeyEvent.GetKey() == EKeys::BackSpace)
	{
		if (Selection)
		{
			Selection->OnRemoved();
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE