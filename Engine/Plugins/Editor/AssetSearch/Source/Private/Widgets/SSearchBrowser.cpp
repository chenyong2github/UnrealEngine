// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSearchBrowser.h"
#include "SSearchTreeRow.h"
#include "EditorFontGlyphs.h"

#include "ClassViewerModule.h"
#include "Kismet2/SClassPickerDialog.h"
#include "Logging/LogMacros.h"
#include "UObject/ReferenceChainSearch.h"
#include "UObject/UObjectIterator.h"
#include "Modules/ModuleManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

// Common classes for the picker
#include "Components/Widget.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Dialogs/Dialogs.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "SearchModel.h"
#include "EditorStyleSet.h"
#include "IAssetSearchModule.h"
#include "AssetRegistryModule.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Layout/SSeparator.h"

#define LOCTEXT_NAMESPACE "SObjectBrowser"

DEFINE_LOG_CATEGORY_STATIC(LogObjectBrowser, Log, All)

namespace AssetSearchConstants
{
	/** The size of the thumbnail pool */
	const int32 ThumbnailPoolSize = 64;
}

void SSearchBrowser::Construct( const FArguments& InArgs )
{
	SortByColumn = SSearchTreeRow::NAME_ColumnName;
	SortMode = EColumnSortMode::Ascending;

	const bool bAreRealTimeThumbnailsAllowed = false;
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(AssetSearchConstants::ThumbnailPoolSize, bAreRealTimeThumbnailsAllowed);

	AssetRegistry = &FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	ChildSlot
	[
		SNew(SBorder)
		.Padding(FMargin(3))
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.OnClicked(this, &SSearchBrowser::OnRefresh)
					[
						SNew(STextBlock)
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
						.Text(FEditorFontGlyphs::Refresh)
					]
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SEditableTextBox)
					.ClearKeyboardFocusOnCommit(false)
					.HintText(LOCTEXT("SearchHint", "Search"))
					.OnTextCommitted(this, &SSearchBrowser::OnSearchTextCommited)
					.OnTextChanged(this, &SSearchBrowser::OnSearchTextChanged)
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(FMargin(0.0f, 4.0f))
				[
					SAssignNew(SearchTreeView, STreeView< TSharedPtr<FSearchNode> >)
					.ItemHeight(24.0f)
					.TreeItemsSource(&SearchResults)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SSearchBrowser::HandleListGenerateRow)
					.OnGetChildren(this, &SSearchBrowser::GetChildrenForInfo)
					.OnSelectionChanged(this, &SSearchBrowser::HandleListSelectionChanged)
					.HeaderRow
					(
						SNew(SHeaderRow)

						+ SHeaderRow::Column(SSearchTreeRow::NAME_ColumnName)
						.DefaultLabel(LOCTEXT("ColumnName", "Name"))
						.FillWidth(0.80f)

						+ SHeaderRow::Column(SSearchTreeRow::NAME_ColumnType)
						.ManualWidth(300)
						.DefaultLabel(LOCTEXT("ColumnType", "Type"))
						//.SortMode(this, &SSearchBrowser::GetColumnSortMode, SObjectBrowserTableRow::CategoryClass)
						//.OnSort(this, &SSearchBrowser::OnColumnSortModeChanged)

					//	+ SHeaderRow::Column(SObjectBrowserTableRow::CategoryWorld)
					//	.FixedWidth(22)
					//	.DefaultLabel(LOCTEXT("CategoryWorld", ""))
					//	.SortMode(this, &SSearchBrowser::GetColumnSortMode, SObjectBrowserTableRow::CategoryWorld)
					//	.OnSort(this, &SSearchBrowser::OnColumnSortModeChanged)

					//	+ SHeaderRow::Column(SObjectBrowserTableRow::CategoryClass)
					//	.ManualWidth(300)
					//	.DefaultLabel(LOCTEXT("CategoryAsset", "Asset"))
					//	.SortMode(this, &SSearchBrowser::GetColumnSortMode, SObjectBrowserTableRow::CategoryClass)
					//	.OnSort(this, &SSearchBrowser::OnColumnSortModeChanged)

					//	+ SHeaderRow::Column(SObjectBrowserTableRow::CategoryProperty)
					//	.FillWidth(1.0f)
					//	.DefaultLabel(LOCTEXT("CategoryProperty", "Text"))
					//	.SortMode(this, &SSearchBrowser::GetColumnSortMode, SObjectBrowserTableRow::CategoryProperty)
					//	.OnSort(this, &SSearchBrowser::OnColumnSortModeChanged)

					//	//+ SHeaderRow::Column(SObjectBrowserTableRow::CategoryPropertyValue)
					//	//.DefaultLabel(LOCTEXT("CategoryPropertyValue", "Value"))
					//	//.SortMode(this, &SSearchBrowser::GetColumnSortMode, SObjectBrowserTableRow::CategoryPropertyValue)
					//	//.OnSort(this, &SSearchBrowser::OnColumnSortModeChanged)
					)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 1)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 1)
			[
				SNew(SHorizontalBox)

				// Asset Stats 
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.Padding(8, 0)
				[
					SNew(STextBlock)
					.Text(this, &SSearchBrowser::GetStatusText)
				]
			]
		]
	];

	RefreshList();
}

FText SSearchBrowser::GetStatusText() const
{
	IAssetSearchModule& SearchModule = IAssetSearchModule::Get();
	FSearchStats SearchStats = SearchModule.GetStats();
	return FText::Format(LOCTEXT("SearchStatusTextFmt", "Scanning {0}   Downloading {1}   Updating {2}            Total Records {3}"), SearchStats.Scanning, SearchStats.Downloading, SearchStats.PendingDatabaseUpdates, SearchStats.TotalRecords);
}

FReply SSearchBrowser::OnRefresh()
{
	RefreshList();

	return FReply::Handled();
}

EColumnSortMode::Type SSearchBrowser::GetColumnSortMode(const FName ColumnId) const
{
	if (SortByColumn == ColumnId)
	{
		return SortMode;
	}

	return EColumnSortMode::None;
}

void SSearchBrowser::OnColumnSortModeChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnId, const EColumnSortMode::Type InSortMode)
{
	SortByColumn = ColumnId;
	SortMode = InSortMode;

	RefreshList();
}

void SSearchBrowser::RefreshList()
{
	SearchResults.Reset();
	SearchResultHierarchy.Reset();

	SearchTreeView->RequestListRefresh();

	if (!FilterText.IsEmpty())
	{
		FSearchQuery Query;
		Query.Query = FilterText.ToString();

		IAssetSearchModule& SearchModule = IAssetSearchModule::Get();
		SearchModule.Search(Query, [this](TArray<FSearchRecord>&& InResults) {

			SearchResults.Reset();
			SearchResultHierarchy.Reset();

			for (int32 ResultIndex = 0; ResultIndex < InResults.Num(); ResultIndex++)
			{
				AppendResult(MoveTemp(InResults[ResultIndex]));
			}

			for (auto& Entry : SearchResultHierarchy)
			{
				SearchResults.Add(Entry.Value);

				SearchTreeView->SetItemExpansion(Entry.Value, true);
			}

			SearchResults.Sort([](const TSharedPtr<FSearchNode>& A, const TSharedPtr<FSearchNode>& B) {
				return A->GetMaxScore() < B->GetMaxScore();
			});

			SearchTreeView->RequestListRefresh();
		});
	}
}

void SSearchBrowser::AppendResult(FSearchRecord&& InResult)
{
	TSharedPtr<FAssetNode> ExistingAssetNode = SearchResultHierarchy.FindRef(InResult.AssetPath);
	if (!ExistingAssetNode.IsValid())
	{
		ExistingAssetNode = MakeShared<FAssetNode>(InResult);
		SearchResultHierarchy.Add(InResult.AssetPath, ExistingAssetNode);
	}
	else
	{
		ExistingAssetNode->Append(InResult);
	}
}

void SSearchBrowser::OnSearchTextCommited(const FText& InText, ETextCommit::Type InCommitType)
{
	TryRefreshingSearch(InText);
}

void SSearchBrowser::OnSearchTextChanged(const FText& InText)
{
	if (InText.ToString().Len() > 3)
	{
		TryRefreshingSearch(InText);
	}
}

void SSearchBrowser::TryRefreshingSearch(const FText& InText)
{
	if (FilterText.ToString() != InText.ToString())
	{
		FilterText = InText;
		FilterString = FilterText.ToString();

		RefreshList();
	}
}

TSharedRef<ITableRow> SSearchBrowser::HandleListGenerateRow(TSharedPtr<FSearchNode> ObjectPtr, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SSearchTreeRow, OwnerTable, AssetRegistry, ThumbnailPool)
		.Object(ObjectPtr)
		.HighlightText(FilterText);
}

void SSearchBrowser::GetChildrenForInfo(TSharedPtr<FSearchNode> InNode, TArray< TSharedPtr<FSearchNode> >& OutChildren)
{
	InNode->GetChildren(OutChildren);
}

void SSearchBrowser::HandleListSelectionChanged(TSharedPtr<FSearchNode> InNode, ESelectInfo::Type SelectInfo)
{
}

#undef LOCTEXT_NAMESPACE
