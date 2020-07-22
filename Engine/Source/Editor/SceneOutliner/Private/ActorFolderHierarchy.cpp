#include "ActorFolderHierarchy.h"

#include "ISceneOutlinerMode.h"
#include "WorldTreeItem.h"
#include "ActorFolderTreeItem.h"
#include "EditorActorFolders.h"

namespace SceneOutliner
{
	FActorFolderHierarchy::FActorFolderHierarchy(ISceneOutlinerMode* InMode, const TWeakObjectPtr<UWorld>& World)
		: ISceneOutlinerHierarchy(InMode)
		, RepresentingWorld(World)
	{
		// ActorFolderHierarchy should only be used with a mode which is showing folders
		check(Mode->ShouldShowFolders());
	}

	FTreeItemPtr FActorFolderHierarchy::FindParent(const ITreeItem& Item, const TMap<FTreeItemID, FTreeItemPtr>& Items) const
	{
		if (Item.IsA<FWorldTreeItem>())
		{
			return nullptr;
		}
		else if (const FActorFolderTreeItem* ActorFolderItem = Item.CastTo<FActorFolderTreeItem>())
		{
			const FName ParentPath = GetParentPath(ActorFolderItem->Path);

			const FTreeItemPtr* ParentItem = nullptr;
			// If the folder has no parent path, it must be parented to the root world
			if (ParentPath.IsNone())
			{
				ParentItem = Items.Find(ActorFolderItem->World.Get());
			}
			else
			{
				ParentItem = Items.Find(GetParentPath(ActorFolderItem->Path));
			}

			if (ParentItem)
			{
				return *ParentItem;
			}
		}
		return nullptr;
	}

	void FActorFolderHierarchy::FindChildren(const ITreeItem& Item, const TMap<FTreeItemID, FTreeItemPtr>& Items, TArray<FTreeItemPtr>& OutChildren) const
	{
		if (const FWorldTreeItem* WorldTreeItem = Item.CastTo<FWorldTreeItem>())
		{
			// Find folders which are located at the root
			for (const auto& Pair : FActorFolders::Get().GetFolderPropertiesForWorld(*WorldTreeItem->World))
			{
				if (const FTreeItemPtr* PotentialChild = Items.Find(Pair.Key))
				{
					if (const FFolderTreeItem* FolderItem = (*PotentialChild)->CastTo<FFolderTreeItem>())
					{
						if (GetParentPath(FolderItem->Path).IsNone())
						{
							OutChildren.Add(*PotentialChild);
						}
					}
				}
			}
		}
		else if (const FFolderTreeItem* FolderItem = Item.CastTo<FFolderTreeItem>())
		{
			// Search through all items and see if there is an item with a path which is a child of this one
			for (const auto& Pair : Items)
			{
				if (const FFolderTreeItem* PotentialChild = Pair.Value->CastTo<FFolderTreeItem>())
				{
					if (PathIsChildOf(PotentialChild->Path, FolderItem->Path))
					{
						OutChildren.Add(Pair.Value);
					}
				}
			}
		}
	}

	void FActorFolderHierarchy::CreateWorldChildren(UWorld* World, TArray<FTreeItemPtr>& OutItems) const
	{
		for (const auto& Pair : FActorFolders::Get().GetFolderPropertiesForWorld(*World))
		{
			if (FTreeItemPtr FolderItem = Mode->CreateItemFor<FActorFolderTreeItem>(FActorFolderTreeItem(Pair.Key, World)))
			{
				OutItems.Add(FolderItem);
			}
		}
	}

	void FActorFolderHierarchy::CreateItems(TArray<FTreeItemPtr>& OutItems) const
	{
		check(RepresentingWorld.IsValid());

		if (FTreeItemPtr WorldItem = Mode->CreateItemFor<FWorldTreeItem>(RepresentingWorld.Get()))
		{
			OutItems.Add(WorldItem);
		}

		CreateWorldChildren(RepresentingWorld.Get(), OutItems);
	}

	void FActorFolderHierarchy::CreateChildren(const FTreeItemPtr& Item, TArray<FTreeItemPtr>& OutChildren) const
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
				if (PathIsChildOf(Pair.Key, FolderItem->Path))
				{
					if (FTreeItemPtr NewFolderItem = Mode->CreateItemFor<FActorFolderTreeItem>(FActorFolderTreeItem(Pair.Key, FolderItem->World), true))
					{
						OutChildren.Add(NewFolderItem);
					}
				}
			}
		}
	}

	FTreeItemPtr FActorFolderHierarchy::CreateParentItem(const FTreeItemPtr& Item) const
	{
		if (Item->IsA<FWorldTreeItem>())
		{
			return nullptr;
		}
		else if (const FActorFolderTreeItem* FolderTreeItem = Item->CastTo<FActorFolderTreeItem>())
		{
			const FName ParentPath = GetParentPath(FolderTreeItem->Path);
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
}