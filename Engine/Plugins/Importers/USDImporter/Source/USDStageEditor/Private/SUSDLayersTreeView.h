// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SUSDTreeView.h"

#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STreeView.h"

class AUsdStageActor;

using FUsdLayerViewModelRef = TSharedRef< class FUsdLayerViewModel >;

class SUsdLayersTreeView : public SUsdTreeView< FUsdLayerViewModelRef >
{
public:
	SLATE_BEGIN_ARGS( SUsdLayersTreeView ) {}
	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs, AUsdStageActor* UsdStageActor );
	void Refresh( AUsdStageActor* UsdStageActor, bool bResync );

private:
	virtual TSharedRef< ITableRow > OnGenerateRow( FUsdLayerViewModelRef InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable ) override;
	virtual void OnGetChildren( FUsdLayerViewModelRef InParent, TArray< FUsdLayerViewModelRef >& OutChildren ) const override;

	virtual void SetupColumns() override;

	void BuildUsdLayersEntries( AUsdStageActor* UsdStageActor );

	TSharedPtr< SWidget > ConstructLayerContextMenu();

	bool CanEditSelectedLayer() const;
	void OnEditSelectedLayer();

	bool CanAddSubLayer() const;
	void OnAddSubLayer();
	void OnNewSubLayer();

	bool CanRemoveLayer( FUsdLayerViewModelRef LayerItem ) const;
	bool CanRemoveSelectedLayers() const;
	void OnRemoveSelectedLayers();
};
