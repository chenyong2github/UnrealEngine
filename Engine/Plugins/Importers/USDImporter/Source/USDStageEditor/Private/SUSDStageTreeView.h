// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SUSDTreeView.h"

#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STreeView.h"

#if USE_USD_SDK

#include "USDMemory.h"

#include "USDIncludesStart.h"

#include "pxr/pxr.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"

#include "USDIncludesEnd.h"

#endif // #if USE_USD_SDK

class AUsdStageActor;
enum class EPayloadsTrigger;

using FUsdStageTreeItemRef = TSharedRef< class FUsdStageTreeItem >;
using FUsdStageTreeItemPtr = TSharedPtr< class FUsdStageTreeItem >;

#if USE_USD_SDK

DECLARE_DELEGATE_OneParam( FOnPrimSelected, FString );
DECLARE_DELEGATE_OneParam( FOnAddPrim, FString );

class SUsdStageTreeView : public SUsdTreeView< FUsdStageTreeItemRef >
{
public:
	SLATE_BEGIN_ARGS( SUsdStageTreeView ) {}
		SLATE_EVENT( FOnPrimSelected, OnPrimSelected )
	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs, AUsdStageActor* InUsdStageActor );
	void Refresh( AUsdStageActor* InUsdStageActor );
	void RefreshPrim( const FString& PrimPath, bool bResync );

private:
	virtual TSharedRef< ITableRow > OnGenerateRow( FUsdStageTreeItemRef InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable ) override;
	virtual void OnGetChildren( FUsdStageTreeItemRef InParent, TArray< FUsdStageTreeItemRef >& OutChildren ) const override;

	void ScrollItemIntoView( FUsdStageTreeItemRef TreeItem );
	virtual void OnTreeItemScrolledIntoView( FUsdStageTreeItemRef TreeItem, const TSharedPtr< ITableRow >& Widget ) override ;

	void OnPrimNameCommitted( const FUsdStageTreeItemRef& TreeItem, const FText& InPrimName );
	void OnPrimNameUpdated( const FUsdStageTreeItemRef& TreeItem, const FText& InPrimName, FText& ErrorMessage );

	virtual void SetupColumns() override;
	TSharedPtr< SWidget > ConstructPrimContextMenu();

	void OnToggleAllPayloads( EPayloadsTrigger PayloadsTrigger );

	void OnAddPrim();
	void OnRemovePrim();
	void OnAddReference();
	void OnClearReferences();

	bool CanAddPrim() const;
	bool CanExecutePrimAction() const;

	TOptional< FString > BrowseFile();

	/** Uses TreeItemExpansionStates to travel the tree and call SetItemExpansion */
	void RestoreExpansionStates();
	virtual void RequestListRefresh() override;

	TWeakObjectPtr< AUsdStageActor > UsdStageActor;
	TWeakPtr< FUsdStageTreeItem > PendingRenameItem;

	// So that we can store these across refreshes
	TMap< FString, bool > TreeItemExpansionStates;

	FOnPrimSelected OnPrimSelected;
};

#endif // #if USE_USD_SDK
