// Copyright Epic Games, Inc. All Rights Reserved.

#include "FolderTreeItem.h"
#include "Textures/SlateIcon.h"
#include "Framework/Commands/UIAction.h"
#include "ToolMenus.h"
#include "EditorStyleSet.h"
#include "ScopedTransaction.h"
#include "SceneOutlinerDragDrop.h"
#include "SSceneOutliner.h"

#include "ActorEditorUtils.h"
#include "EditorActorFolders.h"

#define LOCTEXT_NAMESPACE "SceneOutliner_FolderTreeItem"

namespace SceneOutliner
{
const FTreeItemType FFolderTreeItem::Type(&ITreeItem::Type);

bool FFolderPathSelector::operator()(TWeakPtr<ITreeItem> Item, FName& DataOut) const
{
	if (FFolderTreeItem* FolderItem = Item.Pin()->CastTo<FFolderTreeItem>())
	{
		if (FolderItem->IsValid())
		{
			DataOut = FolderItem->Path;
			return true;
		}
	}
	return false;
}


FFolderTreeItem::FFolderTreeItem(FName InPath)
	: ITreeItem(Type)
	, Path(InPath)
	, LeafName(GetFolderLeafName(InPath))
{
}

FFolderTreeItem::FFolderTreeItem(FName InPath, FTreeItemType InType)
	: ITreeItem(InType)
	, Path(InPath)
	, LeafName(GetFolderLeafName(InPath))
{
}

FTreeItemID FFolderTreeItem::GetID() const
{
	return FTreeItemID(Path);
}

FString FFolderTreeItem::GetDisplayString() const
{
	return LeafName.ToString();
}

bool FFolderTreeItem::CanInteract() const
{
	return Flags.bInteractive;
}

void FFolderTreeItem::DuplicateHierarchy(TWeakPtr<SSceneOutliner> WeakOutliner)
{
	TSharedPtr<SSceneOutliner> Outliner = WeakOutliner.Pin();

	if (Outliner.IsValid())
	{
		const FScopedTransaction Transaction(LOCTEXT("UndoAction_DuplicateHierarchy", "Duplicate Folder Hierarchy"));
		Outliner->DuplicateFoldersHierarchy();
	}
}

void FFolderTreeItem::GenerateContextMenu(UToolMenu* Menu, SSceneOutliner& Outliner)
{
	auto SharedOutliner = StaticCastSharedRef<SSceneOutliner>(Outliner.AsShared());

	const FSlateIcon NewFolderIcon(FEditorStyle::GetStyleSetName(), "SceneOutliner.NewFolderIcon");
	
	FToolMenuSection& Section = Menu->AddSection("Section");
	Section.AddMenuEntry("CreateSubFolder", LOCTEXT("CreateSubFolder", "Create Sub Folder"), FText(), NewFolderIcon, FUIAction(FExecuteAction::CreateSP(this, &FFolderTreeItem::CreateSubFolder, TWeakPtr<SSceneOutliner>(SharedOutliner))));
	Section.AddMenuEntry("DuplicateFolderHierarchy", LOCTEXT("DuplicateFolderHierarchy", "Duplicate Hierarchy"), FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateSP(&Outliner, &SSceneOutliner::DuplicateFoldersHierarchy)));
}

}	// namespace SceneOutliner

#undef LOCTEXT_NAMESPACE
