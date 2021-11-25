// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "SceneOutlinerFwd.h"
#include "ISceneOutlinerTreeItem.h"
#include "WorldPartition/DataLayer/DataLayer.h"

struct FDataLayerTreeItem : ISceneOutlinerTreeItem
{
public:
	FDataLayerTreeItem(UDataLayer* InDataLayer);
	UDataLayer* GetDataLayer() const { return DataLayer.Get(); }

	/* Begin ISceneOutlinerTreeItem Implementation */
	virtual bool IsValid() const override { return DataLayer.IsValid(); }
	virtual FSceneOutlinerTreeItemID GetID() const override { return ID; }
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override { return true; }
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;
	virtual bool HasVisibilityInfo() const override { return true; }
	virtual void OnVisibilityChanged(const bool bNewVisibility) override;
	virtual bool GetVisibility() const override;
	virtual bool ShouldShowVisibilityState() const { return true; }
	/* End ISceneOutlinerTreeItem Implementation */

	bool ShouldBeHighlighted() const;
	void SetIsHighlightedIfSelected(bool bInIsHighlightedIfSelected) { bIsHighlighedtIfSelected = bInIsHighlightedIfSelected; }

	static const FSceneOutlinerTreeItemType Type;

	DECLARE_DELEGATE_RetVal_OneParam(bool, FFilterPredicate, const UDataLayer*);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FInteractivePredicate, const UDataLayer*);

	bool Filter(FFilterPredicate Pred) const
	{
		return Pred.Execute(GetDataLayer());
	}

	bool GetInteractiveState(FInteractivePredicate Pred) const
	{
		return Pred.Execute(GetDataLayer());
	}

private:
	TWeakObjectPtr<UDataLayer> DataLayer;
	const FObjectKey ID;
	bool bIsHighlighedtIfSelected;
};