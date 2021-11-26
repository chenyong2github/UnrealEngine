// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SSceneOutliner.h"

class SDataLayerOutliner : public SSceneOutliner
{
public:
	void Construct(const FArguments& InArgs, const FSceneOutlinerInitializationOptions& InitOptions)
	{
		SSceneOutliner::Construct(InArgs, InitOptions);
	}

	SDataLayerOutliner() {}
	virtual ~SDataLayerOutliner() {}

	void CustomAddToToolbar(TSharedPtr<class SHorizontalBox> Toolbar) override;
private:
	bool CanAddSelectedActorsToSelectedDataLayersClicked() const;
	bool CanRemoveSelectedActorsFromSelectedDataLayersClicked() const;
	FReply OnAddSelectedActorsToSelectedDataLayersClicked();
	FReply OnRemoveSelectedActorsFromSelectedDataLayersClicked();
	TArray<class UDataLayer*> GetSelectedDataLayers() const;
};