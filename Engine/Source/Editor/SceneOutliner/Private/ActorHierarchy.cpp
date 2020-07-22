// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActorHierarchy.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "WorldTreeItem.h"
#include "ActorTreeItem.h"
#include "ComponentTreeItem.h"
#include "ActorFolderTreeItem.h"
#include "ISceneOutlinerMode.h"
#include "ActorEditorUtils.h"
#include "LevelUtils.h"
#include "GameFramework/WorldSettings.h"
#include "EditorActorFolders.h"

namespace SceneOutliner
{
	TUniquePtr<FActorHierarchy> FActorHierarchy::Create(ISceneOutlinerMode* Mode, const TWeakObjectPtr<UWorld>& World, TFunction<bool(const AActor*)> InIsActorDisplayable)
	{
		FActorHierarchy* Hierarchy = new FActorHierarchy(Mode, World, InIsActorDisplayable);
		
		GEngine->OnLevelActorAdded().AddRaw(Hierarchy, &FActorHierarchy::OnLevelActorAdded);
		GEngine->OnLevelActorDeleted().AddRaw(Hierarchy, &FActorHierarchy::OnLevelActorDeleted);
		GEngine->OnLevelActorDetached().AddRaw(Hierarchy, &FActorHierarchy::OnLevelActorDetached);
		GEngine->OnLevelActorAttached().AddRaw(Hierarchy, &FActorHierarchy::OnLevelActorAttached);
		GEngine->OnLevelActorFolderChanged().AddRaw(Hierarchy, &FActorHierarchy::OnLevelActorFolderChanged);
		GEngine->OnLevelActorListChanged().AddRaw(Hierarchy, &FActorHierarchy::OnLevelActorListChanged);

		FWorldDelegates::LevelAddedToWorld.AddRaw(Hierarchy, &FActorHierarchy::OnLevelAdded);
		FWorldDelegates::LevelRemovedFromWorld.AddRaw(Hierarchy, &FActorHierarchy::OnLevelRemoved);

		auto& Folders = FActorFolders::Get();
		Folders.OnFolderCreate.AddRaw(Hierarchy, &FActorHierarchy::OnBroadcastFolderCreate);
		Folders.OnFolderMove.AddRaw(Hierarchy, &FActorHierarchy::OnBroadcastFolderMove);
		Folders.OnFolderDelete.AddRaw(Hierarchy, &FActorHierarchy::OnBroadcastFolderDelete);

		return TUniquePtr<FActorHierarchy>(Hierarchy);
	}

	FActorHierarchy::FActorHierarchy(ISceneOutlinerMode* Mode, const TWeakObjectPtr<UWorld>& World, TFunction<bool(const AActor*)> InIsActorDisplayable)
		: ISceneOutlinerHierarchy(Mode)
		, RepresentingWorld(World)
		, IsActorDisplayableCallback(InIsActorDisplayable)
	{
	}

	FActorHierarchy::~FActorHierarchy()
	{
		if (GEngine)
		{
			GEngine->OnLevelActorAdded().RemoveAll(this);
			GEngine->OnLevelActorDeleted().RemoveAll(this);
			GEngine->OnLevelActorDetached().RemoveAll(this);
			GEngine->OnLevelActorAttached().RemoveAll(this);
			GEngine->OnLevelActorFolderChanged().RemoveAll(this);
			GEngine->OnLevelActorListChanged().RemoveAll(this);
		}

		FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
		FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);

		
		if (FActorFolders::IsAvailable())
		{
			auto& Folders = FActorFolders::Get();
			Folders.OnFolderCreate.RemoveAll(this);
			Folders.OnFolderMove.RemoveAll(this);
			Folders.OnFolderDelete.RemoveAll(this);
		}
	}

	void FActorHierarchy::FindChildren(const ITreeItem& Item, const TMap<FTreeItemID, FTreeItemPtr>& Items, TArray<FTreeItemPtr>& OutChildren) const
	{
		if (const FWorldTreeItem* WorldTreeItem = Item.CastTo<FWorldTreeItem>())
		{
			// All actors in the world are children of the world
			for (FActorIterator ActorIt(WorldTreeItem->World.Get()); ActorIt; ++ActorIt)
			{
				// Actors which have parents are not direct children of a world
				if (ActorIt->GetAttachParentActor() == nullptr)
				{
					if (const FTreeItemPtr* ChildItem = Items.Find(*ActorIt))
					{
						OutChildren.Add(*ChildItem);
					}
				}
			}

			if (Mode->ShouldShowFolders())
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
		}
		else if (const FActorTreeItem* ActorTreeItem = Item.CastTo<FActorTreeItem>())
		{
			// All attached actors are children of this actor
			TArray<AActor*> AttachedActors;
			ActorTreeItem->Actor->GetAttachedActors(AttachedActors);
			for (const AActor* AttachedActor : AttachedActors)
			{
				if (const FTreeItemPtr* AttachedActorItem = Items.Find(AttachedActor))
				{
					OutChildren.Add(*AttachedActorItem);
				}
			}
			if (bShowingComponents)
			{
				for (const UActorComponent* Component : ActorTreeItem->Actor->GetComponents())
				{
					if (const FTreeItemPtr* ComponentItem = Items.Find(Component))
					{
						OutChildren.Add(*ComponentItem);
					}
				}
			}
		}
		else if (const FFolderTreeItem* FolderItem = Item.CastTo<FFolderTreeItem>())
		{
			// We should never call FindParents on a folder item if folders are not being shown
			check(Mode->ShouldShowFolders());

			// Search through all folders and find any folders with paths which are children of this item
			for (const auto& Pair : FActorFolders::Get().GetFolderPropertiesForWorld(*RepresentingWorld))
			{
				if (PathIsChildOf(Pair.Key, FolderItem->Path))
				{
					if (const FTreeItemPtr* Child = Items.Find(Pair.Key))
					{
						OutChildren.Add(*Child);
					}
				}
			}
		}
	}

	FTreeItemPtr FActorHierarchy::FindParent(const ITreeItem& Item, const TMap<FTreeItemID, FTreeItemPtr>& Items) const
	{
		if (Item.IsA<FWorldTreeItem>())
		{
			return nullptr;
		}
		else if (const FActorTreeItem* ActorTreeItem = Item.CastTo<FActorTreeItem>())
		{
			if (ActorTreeItem->Actor.IsValid())
			{
				if (const AActor* ParentActor = ActorTreeItem->Actor->GetAttachParentActor())
				{
					if (const FTreeItemPtr* ParentItem = Items.Find(ParentActor))
					{
						return *ParentItem;
					}
				}
				else if (Mode->ShouldShowFolders() && !ActorTreeItem->Actor->GetFolderPath().IsNone())
				{
					if (const FTreeItemPtr* ParentItem = Items.Find(ActorTreeItem->Actor->GetFolderPath()))
					{
						return *ParentItem;
					}
				}
				else if (const FTreeItemPtr* ParentItem = Items.Find(ActorTreeItem->Actor->GetWorld()))
				{
					return *ParentItem;
				}
			}
		}
		else if (const FFolderTreeItem* FolderItem = Item.CastTo<FFolderTreeItem>())
		{
			// We should never call FindParents on a folder item if folders are not being shown
			check(Mode->ShouldShowFolders());

			const FName ParentPath = GetParentPath(FolderItem->Path);

			const FTreeItemPtr* ParentItem = nullptr;
			// If the folder has no parent path, it must be parented to the root world
			if (ParentPath.IsNone())
			{
				ParentItem = Items.Find(RepresentingWorld.Get());
			}
			else
			{
				ParentItem = Items.Find(GetParentPath(FolderItem->Path));
			}
			
			if (ParentItem)
			{
				return *ParentItem;
			}
		}
		else if (const FComponentTreeItem* ComponentTreeItem = Item.CastTo<FComponentTreeItem>())
		{
			const AActor* Owner = ComponentTreeItem->Component->GetOwner();
			if (Owner)
			{
				if (const FTreeItemPtr* ParentItem = Items.Find(Owner))
				{
					return *ParentItem;
				}
			}
		}
		return nullptr;
	}

	void FActorHierarchy::CreateComponentItems(const AActor* Actor, TArray<FTreeItemPtr>& OutItems) const
	{
		// Add all this actors components if showing components and the owning actor was created
		if (bShowingComponents)
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (FTreeItemPtr ComponentItem = Mode->CreateItemFor<FComponentTreeItem>(Component))
				{
					OutItems.Add(ComponentItem);
				}
			}
		}
	}

	void FActorHierarchy::CreateWorldChildren(UWorld* World, TArray<FTreeItemPtr>& OutItems) const
	{
		// Create all actor items
		for (FActorIterator ActorIt(World); ActorIt; ++ActorIt)
		{
			if (IsActorDisplayableCallback(*ActorIt))
			{
				if (FTreeItemPtr ActorItem = Mode->CreateItemFor<FActorTreeItem>(*ActorIt))
				{
					OutItems.Add(ActorItem);

					// Create all component items
					CreateComponentItems(*ActorIt, OutItems);
				}
			}
		}

		// Create all folder items
		if (Mode->ShouldShowFolders())
		{
			for (const auto& Pair : FActorFolders::Get().GetFolderPropertiesForWorld(*RepresentingWorld))
			{
				if (FTreeItemPtr FolderItem = Mode->CreateItemFor<FActorFolderTreeItem>(FActorFolderTreeItem(Pair.Key, RepresentingWorld)))
				{
					OutItems.Add(FolderItem);
				}
			}
		}
	}

	void FActorHierarchy::CreateItems(TArray<FTreeItemPtr>& OutItems) const
	{
		check(RepresentingWorld.IsValid());

		if (FTreeItemPtr WorldItem = Mode->CreateItemFor<FWorldTreeItem>(RepresentingWorld))
		{
			OutItems.Add(WorldItem);
		}

		// Create world children regardless of if a world item was created
		CreateWorldChildren(RepresentingWorld.Get(), OutItems);
	}

	void FActorHierarchy::CreateChildren(const FTreeItemPtr& Item, TArray<FTreeItemPtr>& OutChildren) const
	{
		if (FWorldTreeItem* WorldItem = Item->CastTo<FWorldTreeItem>())
		{
			check(WorldItem->World == RepresentingWorld);
			CreateWorldChildren(WorldItem->World.Get(), OutChildren);
		}
		else if (const FActorTreeItem* ParentActorItem = Item->CastTo<FActorTreeItem>())
		{
			const AActor* ParentActor = ParentActorItem->Actor.Get();

			if (IsActorDisplayableCallback(ParentActor))
			{
				CreateComponentItems(ParentActor, OutChildren);

				TArray<AActor*> ChildActors;
				TFunction<bool(AActor*)> GetAttachedActors = [&ChildActors, &GetAttachedActors](AActor* Child)
				{
					ChildActors.Add(Child);
					Child->ForEachAttachedActors(GetAttachedActors);

					// Always continue
					return true;
				};

				// Grab all direct/indirect children of an actor
				ParentActor->ForEachAttachedActors(GetAttachedActors);

				for (auto ChildActor : ChildActors)
				{
					if (FTreeItemPtr ChildActorItem = Mode->CreateItemFor<FActorTreeItem>(ChildActor))
					{
						OutChildren.Add(ChildActorItem);

						CreateComponentItems(ChildActor, OutChildren);
					}
				}
			}
		}
		else if (FActorFolderTreeItem* FolderItem = Item->CastTo<FActorFolderTreeItem>())
		{
			check(Mode->ShouldShowFolders());

			for (const auto& Pair : FActorFolders::Get().GetFolderPropertiesForWorld(*FolderItem->World))
			{
				if (PathIsChildOf(Pair.Key, FolderItem->Path))
				{
					if (FTreeItemPtr NewFolderItem = Mode->CreateItemFor<FActorFolderTreeItem>(FActorFolderTreeItem(Pair.Key, FolderItem->World)))
					{
						OutChildren.Add(NewFolderItem);
					}
				}
			}
		}
	}

	FTreeItemPtr FActorHierarchy::CreateParentItem(const FTreeItemPtr& Item) const
	{
		if (Item->IsA<FWorldTreeItem>())
		{
			return nullptr;
		}
		else if (const FActorTreeItem* ActorTreeItem = Item->CastTo<FActorTreeItem>())
		{
			if (ActorTreeItem->Actor.IsValid())
			{
				AActor* ParentActor = ActorTreeItem->Actor->GetAttachParentActor();
				if (ParentActor)
				{
					return Mode->CreateItemFor<FActorTreeItem>(ParentActor, true);
				}
				// if this item does not belong to a folder (or folders are disabled)
				else if (!Mode->ShouldShowFolders() || ActorTreeItem->Actor->GetFolderPath().IsNone())
				{
					UWorld* OwningWorld = ActorTreeItem->Actor->GetWorld();
					check(OwningWorld);
					return Mode->CreateItemFor<FWorldTreeItem>(OwningWorld, true);
				}
				else
				{
					return Mode->CreateItemFor<FActorFolderTreeItem>(FActorFolderTreeItem(ActorTreeItem->Actor->GetFolderPath(), ActorTreeItem->Actor->GetWorld()), true);
				}
			}
		}
		else if (const FComponentTreeItem* ComponentTreeItem = Item->CastTo<FComponentTreeItem>())
		{
			if (AActor* ParentActor = ComponentTreeItem->Component->GetOwner())
			{
				return Mode->CreateItemFor<FActorTreeItem>(ParentActor, true);
			}
		}
		else if (const FActorFolderTreeItem* FolderTreeItem = Item->CastTo<FActorFolderTreeItem>())
		{
			check(Mode->ShouldShowFolders());
			
			const FName ParentPath = GetParentPath(FolderTreeItem->Path);
			if (ParentPath.IsNone())
			{
				UWorld* OwningWorld = FolderTreeItem->World.Get();
				check(OwningWorld);
				return Mode->CreateItemFor<FWorldTreeItem>(OwningWorld, true);
			}
			else
			{
				return Mode->CreateItemFor<FActorFolderTreeItem>(FActorFolderTreeItem(ParentPath, FolderTreeItem->World), true);
			}
		}
		return nullptr;
	}

	void FActorHierarchy::FullRefreshEvent()
	{
		FHierarchyChangedData EventData;
		EventData.Type = FHierarchyChangedData::FullRefresh;
		
		HierarchyChangedEvent.Broadcast(EventData);
	}

	void FActorHierarchy::OnLevelActorAdded(AActor* InActor)
	{
		if (InActor && RepresentingWorld.Get() == InActor->GetWorld() && IsActorDisplayableCallback(InActor))
		{
			FHierarchyChangedData EventData;
			EventData.Type = FHierarchyChangedData::Added;
			EventData.Item = Mode->CreateItemFor<FActorTreeItem>(InActor);
			HierarchyChangedEvent.Broadcast(EventData);
		}
	}

	void FActorHierarchy::OnLevelActorDeleted(AActor* InActor)
	{
		if (RepresentingWorld.Get() == InActor->GetWorld())
		{
			FHierarchyChangedData EventData;
			EventData.Type = FHierarchyChangedData::Removed;
			EventData.ItemID = InActor;
			HierarchyChangedEvent.Broadcast(EventData);
		}
	}

	void FActorHierarchy::OnLevelActorAttached(AActor* InActor, const AActor* InParent)
	{
		if (RepresentingWorld.Get() == InActor->GetWorld())
		{
			FHierarchyChangedData EventData;
			EventData.Type = FHierarchyChangedData::Moved;
			EventData.ItemID = InActor;
			HierarchyChangedEvent.Broadcast(EventData);
		}
	}

	void FActorHierarchy::OnLevelActorDetached(AActor* InActor, const AActor* InParent)
	{
		if (RepresentingWorld.Get() == InActor->GetWorld())
		{
			FHierarchyChangedData EventData;
			EventData.Type = FHierarchyChangedData::Moved;
			EventData.ItemID = InActor;
			HierarchyChangedEvent.Broadcast(EventData);
		}
	}

	void FActorHierarchy::OnComponentsUpdated()
	{
		FullRefreshEvent();
	}

	void FActorHierarchy::OnLevelActorListChanged()
	{
		FullRefreshEvent();
	}

	void FActorHierarchy::OnLevelAdded(ULevel* InLevel, UWorld* InWorld)
	{
		if (RepresentingWorld.Get() == InWorld)
		{
			FullRefreshEvent();
		}
	}

	void FActorHierarchy::OnLevelRemoved(ULevel* InLevel, UWorld* InWorld)
	{
		if (RepresentingWorld.Get() == InWorld)
		{
			FullRefreshEvent();
		}
	}

	/** Called when a folder is to be created */
	void FActorHierarchy::OnBroadcastFolderCreate(UWorld& InWorld, FName NewPath)
	{
		if (Mode->ShouldShowFolders() && RepresentingWorld.Get() == &InWorld)
		{
			FHierarchyChangedData EventData;
			EventData.Type = FHierarchyChangedData::Added;
			EventData.Item = Mode->CreateItemFor<FActorFolderTreeItem>(FActorFolderTreeItem(NewPath, &InWorld));
			EventData.ItemActions = ENewItemAction::Select | ENewItemAction::Rename;
			HierarchyChangedEvent.Broadcast(EventData);
		}
	}

	/** Called when a folder is to be moved */
	void FActorHierarchy::OnBroadcastFolderMove(UWorld& InWorld, FName OldPath, FName NewPath)
	{
		if (Mode->ShouldShowFolders() && RepresentingWorld.Get() == &InWorld)
		{
			FHierarchyChangedData EventData;
			EventData.Type = FHierarchyChangedData::FolderMoved;
			EventData.ItemID = OldPath;
			EventData.NewPath = NewPath;
			HierarchyChangedEvent.Broadcast(EventData);
		}
	}

	/** Called when a folder is to be deleted */
	void FActorHierarchy::OnBroadcastFolderDelete(UWorld& InWorld, FName Path)
	{
		if (Mode->ShouldShowFolders() && RepresentingWorld.Get() == &InWorld)
		{
			FHierarchyChangedData EventData;
			EventData.Type = FHierarchyChangedData::Removed;
			EventData.ItemID = Path;
			HierarchyChangedEvent.Broadcast(EventData);
		}
	}

	void FActorHierarchy::OnLevelActorFolderChanged(const AActor* InActor, FName OldPath)
	{
		if (Mode->ShouldShowFolders() && RepresentingWorld.Get() == InActor->GetWorld())
		{
			FHierarchyChangedData EventData;
			EventData.Type = FHierarchyChangedData::Moved;
			EventData.ItemID = FTreeItemID(InActor);
			HierarchyChangedEvent.Broadcast(EventData);
		}
	}
}
