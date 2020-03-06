// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableViewBase.h"
#include "SearchModel.h"
#include "AssetThumbnail.h"
#include "Widgets/Views/STreeView.h"

class IDetailsView;
class ITableRow;
class FSearchNode;
class FAssetNode;
class FMenuBuilder;
class IAssetRegistry;

/**
 * Implements the undo history panel.
 */
class SSearchBrowser : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SSearchBrowser) { }
	SLATE_END_ARGS()

public:

	/**
	 * Construct this widget
	 *
	 * @param InArgs The declaration data for this widget.
	 */
	void Construct( const FArguments& InArgs );

public:

private:

	FText GetStatusText() const;

	FReply OnRefresh();

	EColumnSortMode::Type GetColumnSortMode(const FName ColumnId) const;
	void OnColumnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type InSortMode);

	void RefreshList();
	void AppendResult(FSearchRecord&& InResult);

	void OnSearchTextCommited(const FText& InText, ETextCommit::Type InCommitType);
	void OnSearchTextChanged(const FText& InText);
	void TryRefreshingSearch(const FText& InText);

	TSharedRef<ITableRow> HandleListGenerateRow(TSharedPtr<FSearchNode> TransactionInfo, const TSharedRef<STableViewBase>& OwnerTable);
	void GetChildrenForInfo(TSharedPtr<FSearchNode> InInfo, TArray< TSharedPtr<FSearchNode> >& OutChildren);

	void HandleListSelectionChanged(TSharedPtr<FSearchNode> TransactionInfo, ESelectInfo::Type SelectInfo);

private:

	FText FilterText;

	// Filters
	FString FilterString;
	
	TMap<FString, TSharedPtr<FAssetNode>> SearchResultHierarchy;
	TArray< TSharedPtr<FSearchNode> > SearchResults;

	TSharedPtr< STreeView< TSharedPtr<FSearchNode> > > SearchTreeView;

	IAssetRegistry* AssetRegistry;

	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;

	FName SortByColumn;
	EColumnSortMode::Type SortMode;
};
