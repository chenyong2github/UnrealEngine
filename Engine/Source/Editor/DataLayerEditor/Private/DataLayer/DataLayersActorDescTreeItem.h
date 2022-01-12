// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneOutlinerFwd.h"
#include "ActorDescTreeItem.h"
#include "WorldPartition/DataLayer/DataLayer.h"

//////////////////////////////////////////////////////////////////////////
// FDataLayerActorTreeItemData

class FWorldPartitionActorDesc;

struct FDataLayerActorDescTreeItemData
{
	FDataLayerActorDescTreeItemData(const FGuid& InActorGuid, UActorDescContainer* InContainer, UDataLayer* InDataLayer)
		: ActorGuid(InActorGuid)
		, Container(InContainer)
		, DataLayer(InDataLayer)
	{}
	
	const FGuid& ActorGuid;
	UActorDescContainer* const Container;
	TWeakObjectPtr<UDataLayer> DataLayer;
};

//////////////////////////////////////////////////////////////////////////
// FDataLayerActorTreeItem

struct FDataLayerActorDescTreeItem : public FActorDescTreeItem
{
public:
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FFilterPredicate, const FWorldPartitionActorDesc* ActorDesc, const UDataLayer* DataLayer);
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FInteractivePredicate, const FWorldPartitionActorDesc* ActorDesc, const UDataLayer* DataLayer);

	FDataLayerActorDescTreeItem(const FDataLayerActorDescTreeItemData& InData)
		: FActorDescTreeItem(InData.ActorGuid, InData.Container)
		, DataLayer(InData.DataLayer)
		, IDDataLayerActorDesc(FDataLayerActorDescTreeItem::ComputeTreeItemID(InData.ActorGuid, DataLayer.Get()))
	{
	}

	UDataLayer* GetDataLayer() const { return DataLayer.Get(); }
	
	static uint32 ComputeTreeItemID(const FGuid& InActorGuid, const UDataLayer* InDataLayer)
	{
		return HashCombine(GetTypeHash(InActorGuid), GetTypeHash(FObjectKey(InDataLayer)));
	}

	bool Filter(FFilterPredicate Pred) const
	{
		return Pred.Execute(ActorDescHandle.Get(), DataLayer.Get());
	}

	bool GetInteractiveState(FInteractivePredicate Pred) const
	{
		return Pred.Execute(ActorDescHandle.Get(), DataLayer.Get());
	}

	/* Begin ISceneOutlinerTreeItem Implementation */
	virtual bool IsValid() const override { return ActorDescHandle.IsValid() && DataLayer.IsValid(); }
	virtual FSceneOutlinerTreeItemID GetID() const override { return IDDataLayerActorDesc; }
	virtual bool ShouldShowVisibilityState() const { return false; }
	virtual bool HasVisibilityInfo() const override { return false; }
	virtual void OnVisibilityChanged(const bool bNewVisibility) override {}
	virtual bool GetVisibility() const override { return false; }
	/* End ISceneOutlinerTreeItem Implementation */

private:
	TWeakObjectPtr<UDataLayer> DataLayer;
	const uint32 IDDataLayerActorDesc;
};