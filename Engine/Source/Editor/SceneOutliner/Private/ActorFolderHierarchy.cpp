// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorFolderHierarchy.h"
#include "ISceneOutlinerMode.h"
#include "WorldTreeItem.h"
#include "ActorFolderTreeItem.h"
#include "EditorActorFolders.h"
#include "EditorFolderUtils.h"

FActorFolderHierarchy::FActorFolderHierarchy(ISceneOutlinerMode* InMode, const TWeakObjectPtr<UWorld>& World)
	: ISceneOutlinerHierarchy(InMode)
	, RepresentingWorld(World)
{
	// ActorFolderHierarchy should only be used with a mode which is showing folders
	check(Mode->ShouldShowFolders());
}

FSceneOutlinerTreeItemPtr FActorFolderHierarchy::FindParent(const ISceneOutlinerTreeItem& Item, const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items) const
{
	if (Item.IsA<FWorldTreeItem>())
	{
		return nullptr;
	}
	else if (const FActorFolderTreeItem* ActorFolderItem = Item.CastTo<FActorFolderTreeItem>())
	{
		const FName ParentPath = FEditorFolderUtils::GetParentPath(ActorFolderItem->Path);

		const FSceneOutlinerTreeItemPtr* ParentItem = nullptr;
		// If the folder has no parent path, it must be parented to the root world
		if (ParentPath.IsNone())
		{
			ParentItem = Items.Find(ActorFolderItem->World.Get());
		}
		else
		{
			ParentItem = Items.Find(FEditorFolderUtils::GetParentPath(ActorFolderItem->Path));
		}

		if (ParentItem)
		{
			return *ParentItem;
		}
	}
	return nullptr;
}

void FActorFolderHierarchy::CreateWorldChildren(UWorld* World, TArray<FSceneOutlinerTreeItemPtr>& OutItems) const
{
	for (const auto& Pair : FActorFolders::Get().GetFolderPropertiesForWorld(*World))
	{
		if (FSceneOutlinerTreeItemPtr FolderItem = Mode->CreateItemFor<FActorFolderTreeItem>(FActorFolderTreeItem(Pair.Key, World)))
		{
			OutItems.Add(FolderItem);
		}
	}
}

void FActorFolderHierarchy::CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const
{
	check(RepresentingWorld.IsValid());

	if (FSceneOutlinerTreeItemPtr WorldItem = Mode->CreateItemFor<FWorldTreeItem>(RepresentingWorld.Get()))
	{
		OutItems.Add(WorldItem);
	}

	CreateWorldChildren(RepresentingWorld.Get(), OutItems);
}

void FActorFolderHierarchy::CreateChildren(const FSceneOutlinerTreeItemPtr& Item, TArray<FSceneOutlinerTreeItemPtr>& OutChildren) const
{
	if (const FWorldTreeItem* WorldItem = Item->CastTo<FWorldTreeItem>())
	{
		check(WorldItem->World == RepresentingWorld);
		CreateWorldChildren(WorldItem->World.Get(), OutChildren);
	}
	else if (FActorFolderTreeItem* FolderItem = Item->CastTo<FActorFolderTreeItem>())
	{
		// Since no map of folder->children exist for ActorFolders, must iterate through all
		// and manually check the path to know if it a child
		for (const auto& Pair : FActorFolders::Get().GetFolderPropertiesForWorld(*(FolderItem->World)))
		{
			if (FEditorFolderUtils::PathIsChildOf(Pair.Key, FolderItem->Path))
			{
				if (FSceneOutlinerTreeItemPtr NewFolderItem = Mode->CreateItemFor<FActorFolderTreeItem>(FActorFolderTreeItem(Pair.Key, FolderItem->World), true))
				{
					OutChildren.Add(NewFolderItem);
				}
			}
		}
	}
}

FSceneOutlinerTreeItemPtr FActorFolderHierarchy::CreateParentItem(const FSceneOutlinerTreeItemPtr& Item) const
{
	if (Item->IsA<FWorldTreeItem>())
	{
		return nullptr;
	}
	else if (const FActorFolderTreeItem* FolderTreeItem = Item->CastTo<FActorFolderTreeItem>())
	{
		const FName ParentPath = FEditorFolderUtils::GetParentPath(FolderTreeItem->Path);
		if (ParentPath.IsNone())
		{
			UWorld* OwningWorld = FolderTreeItem->World.Get();
			check(OwningWorld);
			return Mode->CreateItemFor<FWorldTreeItem>(OwningWorld, true);
		}
		else
		{
			return Mode->CreateItemFor<FActorFolderTreeItem>(FActorFolderTreeItem(ParentPath, FolderTreeItem->World));
		}
	}
	return nullptr;
}
