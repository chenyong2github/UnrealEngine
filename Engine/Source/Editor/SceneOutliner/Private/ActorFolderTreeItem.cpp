// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorFolderTreeItem.h"
#include "ActorTreeItem.h"
#include "SSceneOutliner.h"
#include "ActorEditorUtils.h"
#include "EditorActorFolders.h"
#include "EditorFolderUtils.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "ISceneOutliner.h"
#include "ISceneOutlinerMode.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"
#include "ActorDescTreeItem.h"
#include "Engine/World.h"
#include "WorldPersistentFolders.h"
#include "Engine/Level.h"
#include "LevelUtils.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceSubsystem.h"

#define LOCTEXT_NAMESPACE "SceneOutliner_ActorFolderTreeItem"

const FSceneOutlinerTreeItemType FActorFolderTreeItem::Type(&FFolderTreeItem::Type);

struct SActorFolderTreeLabel : FSceneOutlinerCommonLabelData, public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SActorFolderTreeLabel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FActorFolderTreeItem& FolderItem, ISceneOutliner& SceneOutliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
	{
		TreeItemPtr = StaticCastSharedRef<FActorFolderTreeItem>(FolderItem.AsShared());
		WeakSceneOutliner = StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared());

		TSharedPtr<SInlineEditableTextBlock> InlineTextBlock = SNew(SInlineEditableTextBlock)
			.Text(this, &SActorFolderTreeLabel::GetDisplayText)
			.HighlightText(SceneOutliner.GetFilterHighlightText())
			.ColorAndOpacity(this, &SActorFolderTreeLabel::GetForegroundColor)
			.OnTextCommitted(this, &SActorFolderTreeLabel::OnLabelCommitted)
			.OnVerifyTextChanged(this, &SActorFolderTreeLabel::OnVerifyItemLabelChanged)
			.IsSelected(FIsSelected::CreateSP(&InRow, &STableRow<FSceneOutlinerTreeItemPtr>::IsSelectedExclusively))
			.IsReadOnly_Lambda([Item = FolderItem.AsShared(), this]()
		{
			return !CanExecuteRenameRequest(Item.Get());
		});

		if (WeakSceneOutliner.Pin()->GetMode()->IsInteractive())
		{
			FolderItem.RenameRequestEvent.BindSP(InlineTextBlock.Get(), &SInlineEditableTextBlock::EnterEditingMode);
		}

		ChildSlot
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FSceneOutlinerDefaultTreeItemMetrics::IconPadding())
			[
				SNew(SBox)
				.WidthOverride(FSceneOutlinerDefaultTreeItemMetrics::IconSize())
				.HeightOverride(FSceneOutlinerDefaultTreeItemMetrics::IconSize())
			[
				SNew(SImage)
				.Image(this, &SActorFolderTreeLabel::GetIcon)
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
			]

		+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(0.0f, 2.0f)
			[
				InlineTextBlock.ToSharedRef()
			]
			];
	}

private:
	TWeakPtr<FActorFolderTreeItem> TreeItemPtr;

	FText GetDisplayText() const
	{
		auto Folder = TreeItemPtr.Pin();
		return Folder.IsValid() ? FText::FromString(Folder->GetDisplayString()) : FText();
	}

	const FSlateBrush* GetIcon() const
	{
		auto TreeItem = TreeItemPtr.Pin();
		if (!TreeItem.IsValid())
		{
			return FEditorStyle::Get().GetBrush(TEXT("SceneOutliner.FolderClosed"));
		}

		if (TreeItem->Flags.bIsExpanded && TreeItem->GetChildren().Num())
		{
			return FEditorStyle::Get().GetBrush(TEXT("SceneOutliner.FolderOpen"));
		}
		else
		{
			return FEditorStyle::Get().GetBrush(TEXT("SceneOutliner.FolderClosed"));
		}
	}

	FSlateColor GetForegroundColor() const
	{
		if (auto BaseColor = FSceneOutlinerCommonLabelData::GetForegroundColor(*TreeItemPtr.Pin()))
		{
			return BaseColor.GetValue();
		}

		return FSlateColor::UseForeground();
	}

	bool OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage)
	{
		auto TreeItem = TreeItemPtr.Pin();
		if (!TreeItem.IsValid())
		{
			OutErrorMessage = LOCTEXT("RenameFailed_TreeItemDeleted", "Tree item no longer exists");
			return false;
		}

		const FText TrimmedLabel = FText::TrimPrecedingAndTrailing(InLabel);

		if (TrimmedLabel.IsEmpty())
		{
			OutErrorMessage = LOCTEXT("RenameFailed_LeftBlank", "Names cannot be left blank");
			return false;
		}

		if (TrimmedLabel.ToString().Len() >= NAME_SIZE)
		{
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("CharCount"), NAME_SIZE);
			OutErrorMessage = FText::Format(LOCTEXT("RenameFailed_TooLong", "Names must be less than {CharCount} characters long."), Arguments);
			return false;
		}

		const FString LabelString = TrimmedLabel.ToString();
		if (TreeItem->GetLeafName().ToString() == LabelString)
		{
			return true;
		}

		int32 Dummy = 0;
		if (LabelString.FindChar('/', Dummy) || LabelString.FindChar('\\', Dummy))
		{
			OutErrorMessage = LOCTEXT("RenameFailed_InvalidChar", "Folder names cannot contain / or \\.");
			return false;
		}

		// Validate that this folder doesn't exist already
		FFolder Folder = TreeItem->GetFolder();
		FName NewPath = Folder.GetParent().GetPath();
		if (NewPath.IsNone())
		{
			NewPath = FName(*LabelString);
		}
		else
		{
			NewPath = FName(*(NewPath.ToString() / LabelString));
		}
		Folder.SetPath(NewPath);

		if (FActorFolders::Get().ContainsFolder(*TreeItem->World.Get(), Folder))
		{
			OutErrorMessage = LOCTEXT("RenameFailed_AlreadyExists", "A folder with this name already exists at this level");
			return false;
		}

		return true;
	}

	void OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo)
	{
		auto TreeItem = TreeItemPtr.Pin();
		if (TreeItem.IsValid() && !InLabel.ToString().Equals(TreeItem->GetLeafName().ToString(), ESearchCase::CaseSensitive))
		{
			// Rename the item
			FFolder Folder = TreeItem->GetFolder();
			FName NewPath = Folder.GetParent().GetPath();
			if (NewPath.IsNone())
			{
				NewPath = FName(*InLabel.ToString());
			}
			else
			{
				NewPath = FName(*(NewPath.ToString() / InLabel.ToString()));
			}
			FFolder TreeItemNewFolder(NewPath, Folder.GetRootObject());

			FActorFolders::Get().RenameFolderInWorld(*TreeItem->World.Get(), Folder, TreeItemNewFolder);

			auto Outliner = WeakSceneOutliner.Pin();
			if (Outliner.IsValid())
			{
				Outliner->SetKeyboardFocus();
			}
		}
	}
};

FActorFolderTreeItem::FActorFolderTreeItem(const FFolder& InFolder, const TWeakObjectPtr<UWorld>& InWorld)
	: FFolderTreeItem(InFolder, Type)
	, World(InWorld)
{
	SetPath(InFolder.GetPath());
}

void FActorFolderTreeItem::OnExpansionChanged()
{
	if (!World.IsValid())
	{
		return;
	}

	// Update the central store of folder properties with this folder's new expansion state
	FActorFolders::Get().SetIsFolderExpanded(*World, GetFolder(), Flags.bIsExpanded);
}

void FActorFolderTreeItem::Delete(const FFolder& InNewParentFolder)
{
	if (!World.IsValid())
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("DeleteFolderTransaction", "Delete Folder"));

	const FFolder::FRootObject& NewParentRootObject = InNewParentFolder.GetRootObject();

	for (const TWeakPtr<ISceneOutlinerTreeItem>& ChildPtr : GetChildren())
	{
		FSceneOutlinerTreeItemPtr Child = ChildPtr.Pin();
		if (const FActorTreeItem* ActorItem = Child->CastTo<FActorTreeItem>())
		{
			if (AActor* Actor = ActorItem->Actor.Get())
			{
				check(Actor->GetFolderRootObject() == NewParentRootObject);
				// When using actor folders, no need to update actors since the folder path resolving is dynamic
				if (!Actor->GetLevel()->IsUsingActorFolders())
				{
					Actor->SetFolderPath_Recursively(InNewParentFolder.GetPath());
				}
			}
		}
		else if (FActorFolderTreeItem* FolderItem = Child->CastTo<FActorFolderTreeItem>())
		{
			// When using actor folders, no need to update child folders since the folder path resolving is dynamic
			if (!FolderItem->GetActorFolder())
			{
				FolderItem->MoveTo(InNewParentFolder);
			}
		}
	}

	FActorFolders::Get().DeleteFolder(*World, GetFolder());
}

void FActorFolderTreeItem::MoveTo(const FFolder& InNewParentFolder)
{
	if (World.IsValid())
	{
		check(InNewParentFolder.GetRootObject() == GetRootObject());
		// Get unique name
		const FFolder NewPath = FActorFolders::Get().GetFolderName(*World, InNewParentFolder, GetLeafName());
		FActorFolders::Get().RenameFolderInWorld(*World, GetFolder(), NewPath);
	}
}

void FActorFolderTreeItem::SetPath(const FName& InNewPath)
{
	FFolderTreeItem::SetPath(InNewPath);

	if (World.IsValid())
	{
		ActorFolder = FWorldPersistentFolders::GetActorFolder(GetFolder(), World.Get());
	}
}

void FActorFolderTreeItem::CreateSubFolder(TWeakPtr<SSceneOutliner> WeakOutliner)
{
	auto Outliner = WeakOutliner.Pin();

	if (Outliner.IsValid() && World.IsValid())
	{
		const FScopedTransaction Transaction(LOCTEXT("UndoAction_CreateFolder", "Create Folder"));

		const FFolder NewFolderName = FActorFolders::Get().GetDefaultFolderName(*World, GetFolder());
		FActorFolders::Get().CreateFolder(*World, NewFolderName);

		// At this point the new folder will be in our newly added list, so select it and open a rename when it gets refreshed
		Outliner->OnItemAdded(NewFolderName, SceneOutliner::ENewItemAction::Select | SceneOutliner::ENewItemAction::Rename);
	}
}

TSharedRef<SWidget> FActorFolderTreeItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	return SNew(SActorFolderTreeLabel, *this, Outliner, InRow);
}

bool FActorFolderTreeItem::ShouldShowPinnedState() const
{
	return GetRootObject() == FFolder::GetDefaultRootObject() && World.IsValid() && World->IsPartitionedWorld();
}

bool FActorFolderTreeItem::CanInteract() const
{
	if (!FFolderTreeItem::CanInteract())
	{
		return false;
	}

	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = World.IsValid() ? World->GetSubsystem<ULevelInstanceSubsystem>() : nullptr)
	{
		if (ALevelInstance* EditingLevelInstance = LevelInstanceSubsystem->GetEditingLevelInstance())
		{
			if (GetRootObject() != FFolder::FRootObject(EditingLevelInstance))
			{
				return false;
			}
		}
	}

	ULevel* Level = ActorFolder.IsValid() ? ActorFolder->GetOuterULevel() : nullptr;
	return Level ? !FLevelUtils::IsLevelLocked(Level) : true;
}

#undef LOCTEXT_NAMESPACE
