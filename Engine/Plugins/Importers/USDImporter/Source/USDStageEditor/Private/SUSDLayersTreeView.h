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

	// Drag and drop interface for our rows
	FReply OnRowDragDetected( const FGeometry& Geometry, const FPointerEvent& PointerEvent );
	void OnRowDragLeave( const FDragDropEvent& Event );
	TOptional<EItemDropZone> OnRowCanAcceptDrop( const FDragDropEvent& Event, EItemDropZone Zone, FUsdLayerViewModelRef Item );
	FReply OnRowAcceptDrop( const FDragDropEvent& Event, EItemDropZone Zone, FUsdLayerViewModelRef Item );
	// End drag and drop interface

private:
	virtual TSharedRef< ITableRow > OnGenerateRow( FUsdLayerViewModelRef InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable ) override;
	virtual void OnGetChildren( FUsdLayerViewModelRef InParent, TArray< FUsdLayerViewModelRef >& OutChildren ) const override;

	virtual void SetupColumns() override;

	void BuildUsdLayersEntries( AUsdStageActor* UsdStageActor );

	TSharedPtr< SWidget > ConstructLayerContextMenu();

	bool CanEditSelectedLayer() const;
	void OnEditSelectedLayer();

	void OnClearSelectedLayers();
	bool CanClearSelectedLayers() const;

	void OnSaveSelectedLayers();
	bool CanSaveSelectedLayers() const;

	void OnExportSelectedLayers() const;

	bool CanInsertSubLayer() const;
	void OnAddSubLayer();
	void OnNewSubLayer();

	bool CanRemoveLayer( FUsdLayerViewModelRef LayerItem ) const;
	bool CanRemoveSelectedLayers() const;
	void OnRemoveSelectedLayers();
};
