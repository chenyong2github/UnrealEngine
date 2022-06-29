// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "ActorBaseTreeItem.h"
#include "UObject/ObjectKey.h"
#include "WorldPartition/WorldPartition.h"

class FWorldPartitionActorDesc;
class UToolMenu;

/** A tree item that represents an actor in the world */
struct SCENEOUTLINER_API FActorDescTreeItem : IActorBaseTreeItem
{
public:
	DECLARE_DELEGATE_RetVal_OneParam(bool, FFilterPredicate, const FWorldPartitionActorDesc*);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FInteractivePredicate, const FWorldPartitionActorDesc*);

	bool Filter(FFilterPredicate Pred) const
	{
		return Pred.Execute(ActorDescHandle.Get());
	}

	bool GetInteractiveState(FInteractivePredicate Pred) const
	{
		return Pred.Execute(ActorDescHandle.Get());
	}

	/** The actor desc this tree item is associated with. */
	FWorldPartitionHandle ActorDescHandle;

	/** Constant identifier for this tree item */
	FSceneOutlinerTreeItemID ID;

	/** Static type identifier for this tree item class */
	static const FSceneOutlinerTreeItemType Type;

	/** Construct this item from an actor desc */
	FActorDescTreeItem(const FGuid& InActorGuid, UActorDescContainer* Container);

	/* Begin ISceneOutlinerTreeItem Implementation */
	virtual bool IsValid() const override { return ActorDescHandle.Get() != nullptr; }
	virtual FSceneOutlinerTreeItemID GetID() const override;
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override;
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;
	virtual void GenerateContextMenu(UToolMenu* Menu, SSceneOutliner& Outliner) override;
	virtual bool HasVisibilityInfo() const override { return true; }
	virtual bool GetVisibility() const override;
	virtual bool ShouldShowPinnedState() const override { return true; }
	virtual bool ShouldShowVisibilityState() const override { return false; }
	virtual bool HasPinnedStateInfo() const override { return true; }
	virtual bool GetPinnedState() const override;
	/* End ISceneOutlinerTreeItem Implementation */
	
	/* Begin IActorBaseTreeItem Implementation */
	virtual const FGuid& GetGuid() const override { return ActorGuid; }
	/* End IActorBaseTreeItem Implementation */

protected:
	FString DisplayString;

private:
	void FocusActorBounds() const;
	void CopyActorFilePathtoClipboard() const;
	FGuid ActorGuid;
};
