// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SUSDTreeView.h"

#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STreeView.h"

#include "USDMemory.h"
#include "USDPrimViewModel.h"

class AUsdStageActor;
enum class EPayloadsTrigger;

#if USE_USD_SDK

DECLARE_DELEGATE_OneParam( FOnPrimSelectionChanged, const TArray<FString>& /* NewSelection */);
DECLARE_DELEGATE_OneParam( FOnAddPrim, FString );

class SUsdStageTreeView : public SUsdTreeView< FUsdPrimViewModelRef >
{
public:
	SLATE_BEGIN_ARGS( SUsdStageTreeView ) {}
		SLATE_EVENT( FOnPrimSelectionChanged, OnPrimSelectionChanged )
	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs, AUsdStageActor* InUsdStageActor );
	void Refresh( AUsdStageActor* InUsdStageActor );
	void RefreshPrim( const FString& PrimPath, bool bResync );

	FUsdPrimViewModelPtr GetItemFromPrimPath( const FString& PrimPath );

	void SelectPrims( const TArray<FString>& PrimPaths );
	TArray<FString> GetSelectedPrims();

private:
	virtual TSharedRef< ITableRow > OnGenerateRow( FUsdPrimViewModelRef InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable ) override;
	virtual void OnGetChildren( FUsdPrimViewModelRef InParent, TArray< FUsdPrimViewModelRef >& OutChildren ) const override;

	void ScrollItemIntoView( FUsdPrimViewModelRef TreeItem );
	virtual void OnTreeItemScrolledIntoView( FUsdPrimViewModelRef TreeItem, const TSharedPtr< ITableRow >& Widget ) override ;

	void OnPrimNameCommitted( const FUsdPrimViewModelRef& TreeItem, const FText& InPrimName );
	void OnPrimNameUpdated( const FUsdPrimViewModelRef& TreeItem, const FText& InPrimName, FText& ErrorMessage );

	virtual void SetupColumns() override;
	TSharedPtr< SWidget > ConstructPrimContextMenu();

	void OnToggleAllPayloads( EPayloadsTrigger PayloadsTrigger );

	void OnAddPrim();
	void OnRenamePrim();
	void OnRemovePrim();
	void OnAddReference();
	void OnClearReferences();

	bool CanAddPrim() const;
	bool CanExecutePrimAction() const;

	/** Uses TreeItemExpansionStates to travel the tree and call SetItemExpansion */
	void RestoreExpansionStates();
	virtual void RequestListRefresh() override;

private:
	TWeakObjectPtr< AUsdStageActor > UsdStageActor;
	TWeakPtr< FUsdPrimViewModel > PendingRenameItem;

	// So that we can store these across refreshes
	TMap< FString, bool > TreeItemExpansionStates;

	FOnPrimSelectionChanged OnPrimSelectionChanged;
};

#endif // #if USE_USD_SDK
