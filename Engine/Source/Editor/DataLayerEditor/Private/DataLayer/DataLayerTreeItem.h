// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "SceneOutlinerFwd.h"
#include "ISceneOutlinerTreeItem.h"

class UDataLayerInstance;

struct FDataLayerTreeItem : ISceneOutlinerTreeItem
{
public:
	FDataLayerTreeItem(UDataLayerInstance* InDataLayerInstance);
	UDataLayerInstance* GetDataLayer() const { return DataLayerInstance.Get(); }

	/* Begin ISceneOutlinerTreeItem Implementation */
	virtual bool IsValid() const override { return DataLayerInstance.IsValid(); }
	virtual FSceneOutlinerTreeItemID GetID() const override { return ID; }
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override;
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;
	virtual bool HasVisibilityInfo() const override { return true; }
	virtual void OnVisibilityChanged(const bool bNewVisibility) override;
	virtual bool GetVisibility() const override;
	virtual bool ShouldShowVisibilityState() const override;
	/* End ISceneOutlinerTreeItem Implementation */

	bool ShouldBeHighlighted() const;
	void SetIsHighlightedIfSelected(bool bInIsHighlightedIfSelected) { bIsHighlighedtIfSelected = bInIsHighlightedIfSelected; }

	static const FSceneOutlinerTreeItemType Type;

	DECLARE_DELEGATE_RetVal_OneParam(bool, FFilterPredicate, const UDataLayerInstance*);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FInteractivePredicate, const UDataLayerInstance*);

	bool Filter(FFilterPredicate Pred) const
	{
		return Pred.Execute(GetDataLayer());
	}

	bool GetInteractiveState(FInteractivePredicate Pred) const
	{
		return Pred.Execute(GetDataLayer());
	}

private:
	TWeakObjectPtr<UDataLayerInstance> DataLayerInstance;
	const FObjectKey ID;
	bool bIsHighlighedtIfSelected;
};