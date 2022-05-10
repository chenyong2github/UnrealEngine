// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "SceneOutlinerFwd.h"
#include "ISceneOutlinerTreeItem.h"

class AWorldDataLayers;

struct FWorldDataLayersTreeItem : ISceneOutlinerTreeItem
{
public:
	FWorldDataLayersTreeItem(AWorldDataLayers* InWorldDataLayers);
	AWorldDataLayers* GetWorldDataLayers() const { return WorldDataLayers.Get(); }

	/* Begin ISceneOutlinerTreeItem Implementation */
	virtual bool IsValid() const override { return WorldDataLayers.IsValid(); }
	virtual FSceneOutlinerTreeItemID GetID() const override { return ID; }
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override;
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;
	virtual bool ShouldShowVisibilityState() const override { return false; }
	/* End ISceneOutlinerTreeItem Implementation */

	int32 GetSortPriority() const;

	static const FSceneOutlinerTreeItemType Type;

	DECLARE_DELEGATE_RetVal_OneParam(bool, FFilterPredicate, const AWorldDataLayers*);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FInteractivePredicate, const AWorldDataLayers*);

	bool Filter(FFilterPredicate Pred) const
	{
		return Pred.Execute(GetWorldDataLayers());
	}

	bool GetInteractiveState(FInteractivePredicate Pred) const
	{
		return Pred.Execute(GetWorldDataLayers());
	}

private:
	bool IsReadOnly() const;
	TWeakObjectPtr<AWorldDataLayers> WorldDataLayers;
	const FObjectKey ID;
};