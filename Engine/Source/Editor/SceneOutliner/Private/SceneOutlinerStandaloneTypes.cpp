// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneOutlinerStandaloneTypes.h"

#include "EditorActorFolders.h"
#include "ITreeItem.h"
#include "ISceneOutliner.h"
#include "ISceneOutlinerMode.h"


#define LOCTEXT_NAMESPACE "SceneOutlinerStandaloneTypes"

namespace SceneOutliner
{
	uint32 FTreeItemType::NextUniqueID = 0;
	const FTreeItemType ITreeItem::Type;

	const FLinearColor FCommonLabelData::DarkColor(0.3f, 0.3f, 0.3f);

	TOptional<FLinearColor> FCommonLabelData::GetForegroundColor(const ITreeItem& TreeItem) const
	{
		if (!TreeItem.IsValid())
		{
			return DarkColor;
		}

		// Darken items that aren't suitable targets for an active drag and drop action
		if (FSlateApplication::Get().IsDragDropping())
		{
			TSharedPtr<FDragDropOperation> DragDropOp = FSlateApplication::Get().GetDragDroppingContent();

			FDragDropPayload DraggedObjects;
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

	bool FCommonLabelData::CanExecuteRenameRequest(const ITreeItem& Item) const
	{
		if (const ISceneOutliner* SceneOutliner = WeakSceneOutliner.Pin().Get())
		{
			return SceneOutliner->CanExecuteRenameRequest(Item);
		}
		return false;
	}

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
