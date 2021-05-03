// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerHierarchy.h"
#include "DataLayerMode.h"
#include "DataLayerActorTreeItem.h"
#include "DataLayerTreeItem.h"
#include "DataLayerMode.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"

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
			return Mode->CreateItemFor<FDataLayerTreeItem>(DataLayer);
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

			TArray<const UDataLayer*> DataLayers = InActor->GetDataLayerObjects();
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

void FDataLayerHierarchy::FullRefreshEvent()
{
	FSceneOutlinerHierarchyChangedData EventData;
	EventData.Type = FSceneOutlinerHierarchyChangedData::FullRefresh;
	HierarchyChangedEvent.Broadcast(EventData);
}
