// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "SceneOutlinerFwd.h"
#include "ISceneOutlinerTreeItem.h"
#include "WorldPartition/DataLayer/DataLayer.h"

struct FDataLayerTreeItem : ISceneOutlinerTreeItem
{
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
	/* End ISceneOutlinerTreeItem Implementation */

	static const FSceneOutlinerTreeItemType Type;

private:
	TWeakObjectPtr<UDataLayer> DataLayer;
	const FObjectKey ID;
};