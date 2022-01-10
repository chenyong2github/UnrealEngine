// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneOutlinerFwd.h"
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
	virtual void CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const override;
	virtual void CreateChildren(const FSceneOutlinerTreeItemPtr& Item, TArray<FSceneOutlinerTreeItemPtr>& OutChildren) const override {}
	virtual FSceneOutlinerTreeItemPtr FindOrCreateParentItem(const ISceneOutlinerTreeItem& Item, const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items, bool bCreate = false) override;
	void SetShowEditorDataLayers(bool bInShowEditorDataLayers) { bShowEditorDataLayers = bInShowEditorDataLayers; }
	void SetShowRuntimeDataLayers(bool bInShowRuntimeDataLayers) { bShowRuntimeDataLayers = bInShowRuntimeDataLayers; }
	void SetShowDataLayerActors(bool bInShowDataLayerActors) { bShowDataLayerActors = bInShowDataLayerActors; }
	void SetShowUnloadedActors(bool bInShowUnloadedActors) { bShowUnloadedActors = bInShowUnloadedActors; }
	void SetShowOnlySelectedActors(bool bInbShowOnlySelectedActors) { bShowOnlySelectedActors = bInbShowOnlySelectedActors; }
	void SetHighlightSelectedDataLayers(bool bInHighlightSelectedDataLayers) { bHighlightSelectedDataLayers = bInHighlightSelectedDataLayers; }

private:
	FDataLayerHierarchy(FDataLayerMode* Mode, const TWeakObjectPtr<UWorld>& Worlds);
	FDataLayerHierarchy(const FDataLayerHierarchy&) = delete;
	FDataLayerHierarchy& operator=(const FDataLayerHierarchy&) = delete;

	void OnWorldPartitionCreated(UWorld* InWorld);
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
	void FullRefreshEvent();
	FSceneOutlinerTreeItemPtr CreateDataLayerTreeItem(UDataLayer* InDataLayer, bool bInForce = false) const;
	bool IsDataLayerPartOfSelection(const UDataLayer* DataLayer) const;

	TWeakObjectPtr<UWorld> RepresentingWorld;
	bool bShowEditorDataLayers;
	bool bShowRuntimeDataLayers;
	bool bShowDataLayerActors;
	bool bShowUnloadedActors;
	bool bShowOnlySelectedActors;
	bool bHighlightSelectedDataLayers;
};