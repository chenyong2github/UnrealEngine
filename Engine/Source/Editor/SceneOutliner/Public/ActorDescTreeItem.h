// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "ISceneOutlinerTreeItem.h"
#include "UObject/ObjectKey.h"

class FWorldPartitionActorDesc;

/** A tree item that represents an actor in the world */
struct SCENEOUTLINER_API FActorDescTreeItem : ISceneOutlinerTreeItem
{
public:
	DECLARE_DELEGATE_RetVal_OneParam(bool, FFilterPredicate, const FWorldPartitionActorDesc*);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FInteractivePredicate, const FWorldPartitionActorDesc*);
		
	bool Filter(FFilterPredicate Pred) const
	{
		return Pred.Execute(ActorDesc);
	}

	bool GetInteractiveState(FInteractivePredicate Pred) const 
	{
		return Pred.Execute(ActorDesc);
	}

	/** The actor this tree item is associated with. */
	const FWorldPartitionActorDesc* ActorDesc;

	/** Constant identifier for this tree item */
	FSceneOutlinerTreeItemID ID;

	/** Static type identifier for this tree item class */
	static const FSceneOutlinerTreeItemType Type;

	/** Construct this item from an actor */
	FActorDescTreeItem(const FWorldPartitionActorDesc* InActorDesc);

	/* Begin ISceneOutlinerTreeItem Implementation */
	virtual bool IsValid() const override { return ActorDesc != nullptr; }
	virtual FSceneOutlinerTreeItemID GetID() const override;
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override;
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;
	virtual void OnVisibilityChanged(const bool bNewVisibility) override;
	virtual bool HasVisibilityInfo() const override { return true; }
	virtual bool GetVisibility() const override;
	/* End ISceneOutlinerTreeItem Implementation */
};
