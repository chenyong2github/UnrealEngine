// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerHierarchy.h"
#include "DataLayerMode.h"
#include "DataLayerActorTreeItem.h"
#include "DataLayersActorDescTreeItem.h"
#include "DataLayerTreeItem.h"
#include "DataLayerMode.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "WorldPartition/WorldPartitionHelpers.h"

TUniquePtr<FDataLayerHierarchy> FDataLayerHierarchy::Create(FDataLayerMode* Mode, const TWeakObjectPtr<UWorld>& World)
{
	return TUniquePtr<FDataLayerHierarchy>(new FDataLayerHierarchy(Mode, World));
}

FDataLayerHierarchy::FDataLayerHierarchy(FDataLayerMode* Mode, const TWeakObjectPtr<UWorld>& World)
	: ISceneOutlinerHierarchy(Mode)
	, RepresentingWorld(World)
	, DataLayerBrowser(StaticCastSharedRef<SDataLayerBrowser>(Mode->GetDataLayerBrowser()->AsShared()))
{
	check(DataLayerBrowser.IsValid());
	DataLayerBrowser.Pin()->OnModeChanged().AddRaw(this, &FDataLayerHierarchy::OnDataLayerBrowserModeChanged);

	if (GEngine)
	{
		GEngine->OnLevelActorAdded().AddRaw(this, &FDataLayerHierarchy::OnLevelActorAdded);
		GEngine->OnLevelActorDeleted().AddRaw(this, &FDataLayerHierarchy::OnLevelActorDeleted);
		GEngine->OnLevelActorListChanged().AddRaw(this, &FDataLayerHierarchy::OnLevelActorListChanged);
	}

	if (World.IsValid())
	{
		if (World->PersistentLevel)
		{
			World->PersistentLevel->OnLoadedActorAddedToLevelEvent.AddRaw(this, &FDataLayerHierarchy::OnLoadedActorAdded);
			World->PersistentLevel->OnLoadedActorRemovedFromLevelEvent.AddRaw(this, &FDataLayerHierarchy::OnLoadedActorRemoved);
		}
		
		if (UWorldPartition* WorldPartition = World->GetWorldPartition())
		{
			WorldPartition->OnActorDescAddedEvent.AddRaw(this, &FDataLayerHierarchy::OnActorDescAdded);
			WorldPartition->OnActorDescRemovedEvent.AddRaw(this, &FDataLayerHierarchy::OnActorDescRemoved);
		}
	}

	UDataLayerEditorSubsystem::Get()->OnDataLayerChanged().AddRaw(this, &FDataLayerHierarchy::OnDataLayerChanged);
	UDataLayerEditorSubsystem::Get()->OnActorDataLayersChanged().AddRaw(this, &FDataLayerHierarchy::OnActorDataLayersChanged);
	FWorldDelegates::LevelAddedToWorld.AddRaw(this, &FDataLayerHierarchy::OnLevelAdded);
	FWorldDelegates::LevelRemovedFromWorld.AddRaw(this, &FDataLayerHierarchy::OnLevelRemoved);
}

FDataLayerHierarchy::~FDataLayerHierarchy()
{
	if (DataLayerBrowser.IsValid())
	{
		DataLayerBrowser.Pin()->OnModeChanged().RemoveAll(this);
	}

	if (GEngine)
	{
		GEngine->OnLevelActorAdded().RemoveAll(this);
		GEngine->OnLevelActorDeleted().RemoveAll(this);
		GEngine->OnLevelActorListChanged().RemoveAll(this);
	}

	if (RepresentingWorld.IsValid())
	{
		if (RepresentingWorld->PersistentLevel)
		{
			RepresentingWorld->PersistentLevel->OnLoadedActorAddedToLevelEvent.RemoveAll(this);
			RepresentingWorld->PersistentLevel->OnLoadedActorRemovedFromLevelEvent.RemoveAll(this);
		}
		
		if (UWorldPartition* WorldPartition = RepresentingWorld->GetWorldPartition())
		{
			WorldPartition->OnActorDescAddedEvent.RemoveAll(this);
			WorldPartition->OnActorDescRemovedEvent.RemoveAll(this);
		}
	}

	UDataLayerEditorSubsystem::Get()->OnDataLayerChanged().RemoveAll(this);
	UDataLayerEditorSubsystem::Get()->OnActorDataLayersChanged().RemoveAll(this);
	FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
	FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
}

void FDataLayerHierarchy::CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const
{
	const AWorldDataLayers* WorldDataLayers = RepresentingWorld.Get()->GetWorldDataLayers();
	if (!WorldDataLayers)
	{
		return;
	}

	WorldDataLayers->ForEachDataLayer([this, &OutItems](UDataLayer* DataLayer)
	{
		if (FSceneOutlinerTreeItemPtr DataLayerItem = Mode->CreateItemFor<FDataLayerTreeItem>(DataLayer))
		{
			OutItems.Add(DataLayerItem);
		}
		return true;
	});

	if (DataLayerBrowser.IsValid() && DataLayerBrowser.Pin()->GetMode() == EDataLayerBrowserMode::DataLayerContents)
	{
		for (AActor* Actor : FActorRange(RepresentingWorld.Get()))
		{
			if (Actor->HasDataLayers())
			{
				for (const UDataLayer* DataLayer : Actor->GetDataLayerObjects())
				{
					if (FSceneOutlinerTreeItemPtr DataLayerActorItem = Mode->CreateItemFor<FDataLayerActorTreeItem>(FDataLayerActorTreeItemData(Actor, const_cast<UDataLayer*>(DataLayer))))
					{
						OutItems.Add(DataLayerActorItem);
					}
				}
			}
		}

		if (UWorldPartition* const WorldPartition = RepresentingWorld.Get()->GetWorldPartition())
		{
			FWorldPartitionHelpers::ForEachActorDesc(WorldPartition, [this, WorldPartition, WorldDataLayers, &OutItems](const FWorldPartitionActorDesc* ActorDesc)
				{
					if (ActorDesc != nullptr && !ActorDesc->IsLoaded())
					{
						for (const FName& DataLayerName : ActorDesc->GetDataLayers())
						{
							if (const UDataLayer* const DataLayer = WorldDataLayers->GetDataLayerFromName(DataLayerName))
							{
								if (const FSceneOutlinerTreeItemPtr ActorDescItem = Mode->CreateItemFor<FDataLayerActorDescTreeItem>(FDataLayerActorDescTreeItemData(ActorDesc->GetGuid(), WorldPartition, const_cast<UDataLayer*>(DataLayer))))
								{
									OutItems.Add(ActorDescItem);
								}
							}
						}
					}
					return true;
				});
		}
	}
}

FSceneOutlinerTreeItemPtr FDataLayerHierarchy::FindParent(const ISceneOutlinerTreeItem& Item, const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items) const
{
	if (Item.IsA<FDataLayerTreeItem>())
	{
		return nullptr;
	}
	else if (const FDataLayerActorTreeItem* DataLayerActorTreeItem = Item.CastTo<FDataLayerActorTreeItem>())
	{
		if (const UDataLayer* DataLayer = DataLayerActorTreeItem->GetDataLayer())
		{
			if (const FSceneOutlinerTreeItemPtr* ParentItem = Items.Find(DataLayer))
			{
				return *ParentItem;
			}
		}
	}
	else if (const FDataLayerActorDescTreeItem* DataLayerActorDescTreeItem = Item.CastTo<FDataLayerActorDescTreeItem>())
	{
		if (const UDataLayer* DataLayer = DataLayerActorDescTreeItem->GetDataLayer())
		{
			if (const FSceneOutlinerTreeItemPtr* ParentItem = Items.Find(DataLayer))
			{
				return *ParentItem;
			}
		}
	}
	return nullptr;
}

FSceneOutlinerTreeItemPtr FDataLayerHierarchy::CreateParentItem(const FSceneOutlinerTreeItemPtr& Item) const
{
	if (Item->IsA<FDataLayerTreeItem>())
	{
		return nullptr;
	}
	else if (FDataLayerActorTreeItem* DataLayerActorTreeItem = Item->CastTo<FDataLayerActorTreeItem>())
	{
		if (UDataLayer* DataLayer = DataLayerActorTreeItem->GetDataLayer())
		{
			return Mode->CreateItemFor<FDataLayerTreeItem>(DataLayer, true);
		}
	}
	else if (FDataLayerActorDescTreeItem* DataLayerActorDescTreeItem = Item->CastTo<FDataLayerActorDescTreeItem>())
	{
		if (UDataLayer* DataLayer = DataLayerActorDescTreeItem->GetDataLayer())
		{
			return Mode->CreateItemFor<FDataLayerTreeItem>(DataLayer, true);
		}
	}
	return nullptr;
}

void FDataLayerHierarchy::OnLevelActorAdded(AActor* InActor)
{
	if (InActor && RepresentingWorld.Get() == InActor->GetWorld())
	{
		if (InActor->HasDataLayers())
		{
			FSceneOutlinerHierarchyChangedData EventData;
			EventData.Type = FSceneOutlinerHierarchyChangedData::Added;

			TArray<const UDataLayer*> DataLayers = InActor->GetDataLayerObjects();
			EventData.Items.Reserve(DataLayers.Num());
			for (const UDataLayer* DataLayer : DataLayers)
			{
				EventData.Items.Add(Mode->CreateItemFor<FDataLayerActorTreeItem>(FDataLayerActorTreeItemData(InActor, const_cast<UDataLayer*>(DataLayer))));
			}
			HierarchyChangedEvent.Broadcast(EventData);
		}
	}
}

void FDataLayerHierarchy::OnActorDataLayersChanged(const TWeakObjectPtr<AActor>& InActor)
{
	AActor* Actor = InActor.Get();
	if (Actor && RepresentingWorld.Get() == Actor->GetWorld())
	{
		FullRefreshEvent();
	}
}

void FDataLayerHierarchy::OnDataLayerChanged(const EDataLayerAction Action, const TWeakObjectPtr<const UDataLayer>& ChangedDataLayer, const FName& ChangedProperty)
{
	const UDataLayer* DataLayer = ChangedDataLayer.Get();
	if ((DataLayer && (RepresentingWorld.Get() == DataLayer->GetWorld())) || (Action == EDataLayerAction::Delete) || (Action == EDataLayerAction::Reset))
	{
		FullRefreshEvent();
	}
}

void FDataLayerHierarchy::OnDataLayerBrowserModeChanged(EDataLayerBrowserMode InMode)
{
	FullRefreshEvent();
}

void FDataLayerHierarchy::OnLevelActorDeleted(AActor* InActor)
{
	if (RepresentingWorld.Get() == InActor->GetWorld())
	{
		if (InActor->HasDataLayers())
		{
			FSceneOutlinerHierarchyChangedData EventData;
			EventData.Type = FSceneOutlinerHierarchyChangedData::Removed;

			const TArray<const UDataLayer*> DataLayers = InActor->GetDataLayerObjects();
			EventData.ItemIDs.Reserve(DataLayers.Num());
			for (const UDataLayer* DataLayer : DataLayers)
			{
				EventData.ItemIDs.Add(FDataLayerActorTreeItem::ComputeTreeItemID(InActor, DataLayer));
			}
			HierarchyChangedEvent.Broadcast(EventData);
		}
	}
}

void FDataLayerHierarchy::OnLevelActorListChanged()
{
	FullRefreshEvent();
}

void FDataLayerHierarchy::OnLevelAdded(ULevel* InLevel, UWorld* InWorld)
{
	if (RepresentingWorld.Get() == InWorld)
	{
		FullRefreshEvent();
	}
}

void FDataLayerHierarchy::OnLevelRemoved(ULevel* InLevel, UWorld* InWorld)
{
	if (RepresentingWorld.Get() == InWorld)
	{
		FullRefreshEvent();
	}
}

void FDataLayerHierarchy::OnLoadedActorAdded(AActor& InActor)
{
	// Handle the actor being added to the level
	OnLevelActorAdded(&InActor);

	// Remove corresponding actor desc items
	if (RepresentingWorld.Get() == InActor.GetWorld() && InActor.HasDataLayers())
	{
		FSceneOutlinerHierarchyChangedData EventData;
		EventData.Type = FSceneOutlinerHierarchyChangedData::Removed;
		
		const TArray<const UDataLayer*> DataLayers = InActor.GetDataLayerObjects();
		EventData.ItemIDs.Reserve(DataLayers.Num());
		for (const UDataLayer* DataLayer : DataLayers)
		{
			EventData.ItemIDs.Add(FDataLayerActorDescTreeItem::ComputeTreeItemID(InActor.GetActorGuid(), DataLayer));
		}
		HierarchyChangedEvent.Broadcast(EventData);
	}
}

void FDataLayerHierarchy::OnLoadedActorRemoved(AActor& InActor)
{
	// Handle the actor being removed from the level
	OnLevelActorDeleted(&InActor);

	// Add any corresponding actor desc items for this actor
	UWorldPartition* const WorldPartition = RepresentingWorld.Get()->GetWorldPartition();
	if (RepresentingWorld.Get() == InActor.GetWorld() && InActor.HasDataLayers() && WorldPartition != nullptr)
	{
		FSceneOutlinerHierarchyChangedData EventData;
		EventData.Type = FSceneOutlinerHierarchyChangedData::Added;

		const TArray<const UDataLayer*> DataLayers = InActor.GetDataLayerObjects();
		EventData.Items.Reserve(DataLayers.Num());
		for (const UDataLayer* DataLayer : DataLayers)
		{
			EventData.Items.Add(Mode->CreateItemFor<FDataLayerActorDescTreeItem>(FDataLayerActorDescTreeItemData(InActor.GetActorGuid(), WorldPartition, const_cast<UDataLayer*>(DataLayer))));
		}
		HierarchyChangedEvent.Broadcast(EventData);
    }
}

void FDataLayerHierarchy::OnActorDescAdded(FWorldPartitionActorDesc* InActorDesc)
{
	if (InActorDesc == nullptr)
	{
		return;
	}

	UWorldPartition* const WorldPartition = RepresentingWorld.Get()->GetWorldPartition();
	const AWorldDataLayers* const WorldDataLayers = RepresentingWorld.Get()->GetWorldDataLayers();
	const TArray<FName>& DataLayerNames = InActorDesc->GetDataLayers();

	if (WorldDataLayers != nullptr && RepresentingWorld->GetWorldPartition() == WorldPartition && DataLayerNames.Num() > 0)
	{
		FSceneOutlinerHierarchyChangedData EventData;
		EventData.Type = FSceneOutlinerHierarchyChangedData::Added;

		EventData.Items.Reserve(DataLayerNames.Num());
		for (const FName& DataLayerName : DataLayerNames)
		{
			const UDataLayer* const DataLayer = WorldDataLayers->GetDataLayerFromName(DataLayerName);
			EventData.Items.Add(Mode->CreateItemFor<FDataLayerActorDescTreeItem>(FDataLayerActorDescTreeItemData(InActorDesc->GetGuid(), WorldPartition, const_cast<UDataLayer*>(DataLayer))));
		}
		HierarchyChangedEvent.Broadcast(EventData);
	}
}

void FDataLayerHierarchy::OnActorDescRemoved(FWorldPartitionActorDesc* InActorDesc)
{
	if (InActorDesc == nullptr)
	{
		return;
	}

	const AWorldDataLayers* const WorldDataLayers = RepresentingWorld.Get()->GetWorldDataLayers();
	const TArray<FName>& DataLayerNames = InActorDesc->GetDataLayers();
	
	if (WorldDataLayers != nullptr && DataLayerNames.Num() > 0)
	{
		FSceneOutlinerHierarchyChangedData EventData;
		EventData.Type = FSceneOutlinerHierarchyChangedData::Removed;
		EventData.ItemIDs.Reserve(DataLayerNames.Num());

		for (const FName& DataLayerName : DataLayerNames)
		{
			const UDataLayer* const DataLayer = WorldDataLayers->GetDataLayerFromName(DataLayerName);
			EventData.ItemIDs.Add(FDataLayerActorDescTreeItem::ComputeTreeItemID(InActorDesc->GetGuid(), DataLayer));
		}
		HierarchyChangedEvent.Broadcast(EventData);
	}
}

void FDataLayerHierarchy::FullRefreshEvent()
{
	FSceneOutlinerHierarchyChangedData EventData;
	EventData.Type = FSceneOutlinerHierarchyChangedData::FullRefresh;
	HierarchyChangedEvent.Broadcast(EventData);
}
