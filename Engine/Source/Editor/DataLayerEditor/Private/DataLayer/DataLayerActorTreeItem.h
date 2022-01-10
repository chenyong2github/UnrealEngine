// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SceneOutlinerFwd.h"
#include "ActorTreeItem.h"
#include "WorldPartition/DataLayer/DataLayer.h"

//////////////////////////////////////////////////////////////////////////
// FDataLayerActorTreeItemData

struct FDataLayerActorTreeItemData
{
	FDataLayerActorTreeItemData(AActor* InActor, UDataLayer* InDataLayer)
		: Actor(InActor)
		, DataLayer(InDataLayer)
	{}
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<UDataLayer> DataLayer;
};

//////////////////////////////////////////////////////////////////////////
// FDataLayerActorTreeItem

struct FDataLayerActorTreeItem : public FActorTreeItem
{
public:
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FFilterPredicate, const AActor*, const UDataLayer* DataLayer);
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FInteractivePredicate, const AActor*, const UDataLayer* DataLayer);

	FDataLayerActorTreeItem(const FDataLayerActorTreeItemData& InData)
		: FActorTreeItem(InData.Actor.Get())
		, DataLayer(InData.DataLayer)
		, IDDataLayerActor(FDataLayerActorTreeItem::ComputeTreeItemID(Actor.Get(), DataLayer.Get()))
	{
	}

	UDataLayer* GetDataLayer() const { return DataLayer.Get(); }
	
	const AActor* GetActor() const { return Actor.Get(); }
	AActor* GetActor() { return Actor.Get(); }

	static uint32 ComputeTreeItemID(const AActor* InActor, const UDataLayer* InDataLayer)
	{
		return HashCombine(GetTypeHash(FObjectKey(InActor)), GetTypeHash(FObjectKey(InDataLayer)));
	}

	bool Filter(FFilterPredicate Pred) const
	{
		return Pred.Execute(Actor.Get(), DataLayer.Get());
	}

	bool GetInteractiveState(FInteractivePredicate Pred) const
	{
		return Pred.Execute(Actor.Get(), DataLayer.Get());
	}

	/* Begin ISceneOutlinerTreeItem Implementation */
	virtual bool IsValid() const override { return Actor.IsValid() && DataLayer.IsValid(); }
	virtual FSceneOutlinerTreeItemID GetID() const override { return IDDataLayerActor; }
	virtual bool ShouldShowVisibilityState() const { return false; }
	virtual bool HasVisibilityInfo() const override { return false; }
	virtual void OnVisibilityChanged(const bool bNewVisibility) override {}
	virtual bool GetVisibility() const override { return false; }
	/* End ISceneOutlinerTreeItem Implementation */

private:
	TWeakObjectPtr<UDataLayer> DataLayer;
	const uint32 IDDataLayerActor;
};