// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneOutlinerStandaloneTypes.h"

#include "EditorActorFolders.h"
#include "ISceneOutlinerTreeItem.h"
#include "ISceneOutliner.h"
#include "ISceneOutlinerMode.h"


#define LOCTEXT_NAMESPACE "SceneOutlinerStandaloneTypes"

uint32 FSceneOutlinerTreeItemType::NextUniqueID = 0;
const FSceneOutlinerTreeItemType ISceneOutlinerTreeItem::Type;

const FLinearColor FSceneOutlinerCommonLabelData::DarkColor(0.15f, 0.15f, 0.15f);

TOptional<FLinearColor> FSceneOutlinerCommonLabelData::GetForegroundColor(const ISceneOutlinerTreeItem& TreeItem) const
{
	if (!TreeItem.IsValid())
	{
		return DarkColor;
	}

	// Darken items that aren't suitable targets for an active drag and drop action
	if (FSlateApplication::Get().IsDragDropping())
	{
		TSharedPtr<FDragDropOperation> DragDropOp = FSlateApplication::Get().GetDragDroppingContent();

		FSceneOutlinerDragDropPayload DraggedObjects;
		const auto Outliner = WeakSceneOutliner.Pin();
		if (Outliner->GetMode()->ParseDragDrop(DraggedObjects, *DragDropOp) && !Outliner->GetMode()->ValidateDrop(TreeItem, DraggedObjects).IsValid())
		{
			return DarkColor;
		}
	}

	if (!TreeItem.CanInteract())
	{
		return DarkColor;
	}

	return TOptional<FLinearColor>();
}

bool FSceneOutlinerCommonLabelData::CanExecuteRenameRequest(const ISceneOutlinerTreeItem& Item) const
{
	if (const ISceneOutliner* SceneOutliner = WeakSceneOutliner.Pin().Get())
	{
		return SceneOutliner->CanExecuteRenameRequest(Item);
	}
	return false;
}

namespace SceneOutliner
{
	/** Parse a new path (including leaf-name) into this tree item. Does not do any notification */
	FName GetFolderLeafName(FName InPath)
	{
		FString PathString = InPath.ToString();
		int32 LeafIndex = 0;
		if (PathString.FindLastChar('/', LeafIndex))
		{
			return FName(*PathString.RightChop(LeafIndex + 1));
		}
		else
		{
			return InPath;
		}
	}

	bool PathIsChildOf(const FName& PotentialChild, const FName& Parent)
	{
		const FString ParentString = Parent.ToString();
		const FString ChildString = PotentialChild.ToString();
		const int32 ParentLen = ParentString.Len();
		return
			ChildString.Len() > ParentLen &&
			ChildString[ParentLen] == '/' &&
			ChildString.Left(ParentLen) == ParentString;
	}

}	// namespace SceneOutliner

#undef LOCTEXT_NAMESPACE
