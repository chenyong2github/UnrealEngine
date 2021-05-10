// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneOutlinerFwd.h"
#include "SDataLayerBrowser.h"
#include "ISceneOutlinerHierarchy.h"
#include "DataLayer/DataLayerAction.h"

class FDataLayerMode;
class UDataLayer;
class UWorld;
class FWorldPartitionActorDesc;

class FDataLayerHierarchy : public ISceneOutlinerHierarchy
{
public:
	virtual ~FDataLayerHierarchy();
	static TUniquePtr<FDataLayerHierarchy> Create(FDataLayerMode* Mode, const TWeakObjectPtr<UWorld>& World);
	virtual FSceneOutlinerTreeItemPtr FindParent(const ISceneOutlinerTreeItem& Item, const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items) const override;
	virtual void CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const override;
	virtual void CreateChildren(const FSceneOutlinerTreeItemPtr& Item, TArray<FSceneOutlinerTreeItemPtr>& OutChildren) const override {}
	virtual FSceneOutlinerTreeItemPtr CreateParentItem(const FSceneOutlinerTreeItemPtr& Item) const override;

private:
	FDataLayerHierarchy(FDataLayerMode* Mode, const TWeakObjectPtr<UWorld>& Worlds);
	FDataLayerHierarchy(const FDataLayerHierarchy&) = delete;
	FDataLayerHierarchy& operator=(const FDataLayerHierarchy&) = delete;

	void OnLevelActorsAdded(const TArray<AActor*>& InActors);
	void OnLevelActorsRemoved(const TArray<AActor*>& InActors);
	void OnLevelActorAdded(AActor* InActor);
	void OnLevelActorDeleted(AActor* InActor);
	void OnLevelActorListChanged();
	void OnLevelAdded(ULevel* InLevel, UWorld* InWorld);
	void OnLevelRemoved(ULevel* InLevel, UWorld* InWorld);
	void OnLoadedActorAdded(AActor& InActor);
	void OnLoadedActorRemoved(AActor& InActor);
	void OnActorDescAdded(FWorldPartitionActorDesc* InActorDesc);
	void OnActorDescRemoved(FWorldPartitionActorDesc* InActorDesc);
	void OnActorDataLayersChanged(const TWeakObjectPtr<AActor>& InActor);
	void OnDataLayerChanged(const EDataLayerAction Action, const TWeakObjectPtr<const UDataLayer>& ChangedDataLayer, const FName& ChangedProperty);
	void OnDataLayerBrowserModeChanged(EDataLayerBrowserMode InMode);
	void FullRefreshEvent();

	TWeakObjectPtr<UWorld> RepresentingWorld;
	TWeakPtr<SDataLayerBrowser> DataLayerBrowser;
};