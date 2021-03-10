// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "ISceneOutlinerTreeItem.h"
#include "UObject/ObjectKey.h"
#include "WorldPartition/WorldPartition.h"

class FWorldPartitionActorDesc;

/** A tree item that represents an actor in the world */
struct SCENEOUTLINER_API FActorDescTreeItem : ISceneOutlinerTreeItem
{
	struct FActorDescHandle
	{
	public:
		FActorDescHandle(const FWorldPartitionActorDesc* InActorDesc, UActorDescContainer* InContainer)
			: ActorDesc(InActorDesc)
			, Container(InContainer)
		{
		}
		const FWorldPartitionActorDesc* GetActorDesc() const
		{
			return Container.IsValid() ? ActorDesc : nullptr;
		}
	private:
		const FWorldPartitionActorDesc* ActorDesc;
		TWeakObjectPtr<UActorDescContainer> Container;
	};

public:
	DECLARE_DELEGATE_RetVal_OneParam(bool, FFilterPredicate, const FWorldPartitionActorDesc*);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FInteractivePredicate, const FWorldPartitionActorDesc*);

	bool Filter(FFilterPredicate Pred) const
	{
		return Pred.Execute(ActorDescHandle.GetActorDesc());
	}

	bool GetInteractiveState(FInteractivePredicate Pred) const
	{
		return Pred.Execute(ActorDescHandle.GetActorDesc());
	}

	/** The actor desc this tree item is associated with. */
	FActorDescHandle ActorDescHandle;

	/** Constant identifier for this tree item */
	FSceneOutlinerTreeItemID ID;

	/** Static type identifier for this tree item class */
	static const FSceneOutlinerTreeItemType Type;

	/** Construct this item from an actor desc */
	FActorDescTreeItem(const FWorldPartitionActorDesc* InActorDesc, UActorDescContainer* Container);

	/* Begin ISceneOutlinerTreeItem Implementation */
	virtual bool IsValid() const override { return ActorDescHandle.GetActorDesc() != nullptr; }
	virtual FSceneOutlinerTreeItemID GetID() const override;
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override;
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;
	virtual void OnVisibilityChanged(const bool bNewVisibility) override;
	virtual bool HasVisibilityInfo() const override { return true; }
	virtual bool GetVisibility() const override;
	/* End ISceneOutlinerTreeItem Implementation */
};
