// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SUSDTreeView.h"

#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STreeView.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"

#include "pxr/pxr.h"
#include "pxr/usd/sdf/path.h"

#include "USDIncludesEnd.h"

#endif // #if USE_USD_SDK

class AUsdStageActor;

using FUsdLayersTreeItemRef = TSharedRef< class FUsdLayersTreeItem >;
using FUsdLayersTreeItemPtr = TSharedPtr< class FUsdLayersTreeItem >;

class SUsdLayersTreeView : public SUsdTreeView< FUsdLayersTreeItemRef >
{
public:
	SLATE_BEGIN_ARGS( SUsdLayersTreeView ) {}
	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs, AUsdStageActor* UsdStageActor );
	void Refresh( AUsdStageActor* UsdStageActor, bool bResync );

private:
	virtual TSharedRef< ITableRow > OnGenerateRow( FUsdLayersTreeItemRef InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable ) override;
	virtual void OnGetChildren( FUsdLayersTreeItemRef InParent, TArray< FUsdLayersTreeItemRef >& OutChildren ) const override;

	virtual void SetupColumns() override;

	void BuildUsdLayersEntries( AUsdStageActor* UsdStageActor );

	TSharedPtr< SWidget > ConstructLayerContextMenu();

	bool CanEditLayer( FUsdLayersTreeItemRef LayerItem ) const;
	bool CanEditSelectedLayer() const;
	void OnEditSelectedLayer();

	void OnAddSubLayer();
	void OnNewSubLayer();

	bool CanRemoveLayer( FUsdLayersTreeItemRef LayerItem ) const;
	bool CanRemoveSelectedLayers() const;
	void OnRemoveSelectedLayers();
};
