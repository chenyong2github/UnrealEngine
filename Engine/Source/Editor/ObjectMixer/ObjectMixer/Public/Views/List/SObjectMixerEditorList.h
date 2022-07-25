// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectMixerEditorList.h"
#include "ObjectMixerEditorListRow.h"
#include "ObjectMixerEditorListFilters/IObjectMixerEditorListFilter.h"

#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STreeView.h"

#include "SObjectMixerEditorList.generated.h"

class SWrapBox;
class FObjectMixerEditorList;
class SBox;
class SComboButton;
class SSearchBox;
class SHeaderRow;

UENUM()
enum class EListViewColumnType
{
	BuiltIn,
	PropertyGenerated
};

USTRUCT()
struct FListViewColumnInfo
{
	GENERATED_BODY()
	
	TObjectPtr<FProperty> PropertyRef;
	
	UPROPERTY()
	FName PropertyName;

	UPROPERTY()
	FText PropertyDisplayText;

	UPROPERTY()
	EListViewColumnType PropertyType;

	UPROPERTY()
	FName CategoryName;

	UPROPERTY()
	bool bIsDesiredForDisplay = false;

	UPROPERTY()
	bool bCanBeSorted = false;

	UPROPERTY()
	bool bUseFixedWidth = false;

	UPROPERTY()
	float FixedWidth = 25.0f;

	UPROPERTY()
	float FillWidth = 1.0f;
};

class OBJECTMIXEREDITOR_API SObjectMixerEditorList final : public SCompoundWidget
{

public:

	SLATE_BEGIN_ARGS(SObjectMixerEditorList)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FObjectMixerEditorList> ListModel);

	virtual ~SObjectMixerEditorList() override;

	TWeakPtr<FObjectMixerEditorList> GetListModelPtr()
	{
		return ListModelPtr;
	}

	void ClearList()
	{
		FlushMemory(false);
	}

	/**
	 * Refresh filters and sorting.
	 * Useful for when the list state has gone stale but the variable count has not changed.
	 */
	void RefreshList();

	/*
	 * Regenerate the list items and refresh the list. Call when adding or removing items.
	 */
	void RequestRebuildList(const FString& InItemToScrollTo = "");

	[[nodiscard]] TArray<FObjectMixerEditorListRowPtr> GetSelectedTreeViewItems() const;
	int32 GetSelectedTreeViewItemCount() const;

	void SetSelectedTreeViewItemActorsEditorVisible(const bool bNewIsVisible);

	bool IsTreeViewItemSelected(TSharedRef<FObjectMixerEditorListRow> Item);

	[[nodiscard]] TArray<FObjectMixerEditorListRowPtr> GetTreeViewItems() const;
	void SetTreeViewItems(const TArray<FObjectMixerEditorListRowPtr>& InItems);

	[[nodiscard]] int32 GetTreeViewItemCount() const
	{
		return TreeViewRootObjects.Num();
	}
	
	TWeakPtr<FObjectMixerEditorListRow> GetSoloRow()
	{
		return GetListModelPtr().Pin()->GetSoloRow();
	}

	void SetSoloRow(TSharedRef<FObjectMixerEditorListRow> InRow)
	{
		GetListModelPtr().Pin()->SetSoloRow(InRow);
	}

	void ClearSoloRow()
	{
		GetListModelPtr().Pin()->ClearSoloRow();
	}

	FString GetSearchStringFromSearchInputField() const;
	void SetSearchStringInSearchInputField(const FString InSearchString) const;
	void ExecuteListViewSearchOnAllRows(const FString& SearchString, const bool bShouldRefreshAfterward = true);

	bool DoesTreeViewHaveVisibleChildren() const;

	void SetTreeViewItemExpanded(const TSharedPtr<FObjectMixerEditorListRow>& RowToExpand, const bool bNewExpansion) const;

	void ToggleFilterActive(const FString& FilterName);
	void EvaluateIfRowsPassFilters(const bool bShouldRefreshAfterward = true);

	// Sorting

	const FName& GetActiveSortingColumnName() const
	{
		return ActiveSortingColumnName;
	}
	EColumnSortMode::Type GetSortModeForColumn(FName InColumnName) const;
	void OnSortColumnCalled(EColumnSortPriority::Type Priority, const FName& ColumnName, EColumnSortMode::Type SortMode);
	EColumnSortMode::Type CycleSortMode(const FName& InColumnName);
	void ExecuteSort(
		const FName& InColumnName, const EColumnSortMode::Type InColumnSortMode, const bool bShouldRefreshAfterward = true);
	void ClearSorting()
	{
		ActiveSortingColumnName = NAME_None;
		ActiveSortingType = EColumnSortMode::None;
	}

	// Columns

	FListViewColumnInfo* GetColumnInfoByPropertyName(const FName& InPropertyName);

	static const FName ItemNameColumnName;
	static const FName EditorVisibilityColumnName;
	static const FName EditorVisibilitySoloColumnName;

private:

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void OnActorSpawnedOrDestroyed(AActor* Object)
	{
		RequestRebuildList();
	}

	TWeakPtr<FObjectMixerEditorList> ListModelPtr;
	
	TSharedPtr<SHeaderRow> HeaderRow;
	TSharedRef<SWidget> GenerateHeaderRowContextMenu() const;

	bool bShouldRebuild = false;

	/*
	 * Regenerate the list items and refresh the list. Call when adding or removing items.
	 */
	void RebuildList();

	/*
	 * Only adds properties that pass a series of tests, including having only one unique entry in the column list array.
	 * @param bForceIncludeProperty If true, only Skiplist and Uniqueness tests will be checked, bypassing class, blueprint editability and other requirements.
	 * @param
	 */
	bool AddUniquePropertyColumnsToHeaderRow(
		FProperty* Property,
		const bool bForceIncludeProperty = false,
		const TArray<FName>& PropertySkipList = {}
	);
	void AddBuiltinColumnsToHeaderRow();
	TSharedPtr<SHeaderRow> GenerateHeaderRow();
	ECheckBoxState HeaderCheckBoxState = ECheckBoxState::Checked;

	void SetupFilters();

	TSharedRef<SWidget> BuildShowOptionsMenu();
	
	void FlushMemory(const bool bShouldKeepMemoryAllocated);
	
	void SetAllGroupsCollapsed();

	// Search
	
	void OnListViewSearchTextChanged(const FText& Text);

	TSharedPtr<SSearchBox> ListSearchBoxPtr;
	TSharedPtr<SComboButton> ViewOptionsComboButton;
	
	TSharedPtr<SBox> ListBoxContainerPtr;

	//  Tree View Implementation

	void GenerateTreeView();
	void FindVisibleTreeViewObjects();
	void FindVisibleObjectsAndRequestTreeRefresh();
	
	void OnGetRowChildren(FObjectMixerEditorListRowPtr Row, TArray<FObjectMixerEditorListRowPtr>& OutChildren) const;
	void OnRowChildExpansionChange(FObjectMixerEditorListRowPtr Row, const bool bIsExpanded, const bool bIsRecursive = false) const;

	void SetChildExpansionRecursively(const FObjectMixerEditorListRowPtr& InRow, const bool bNewIsExpanded) const;

	TArray<TSharedRef<IObjectMixerEditorListFilter>> ShowFilters;

	TSharedPtr<STreeView<FObjectMixerEditorListRowPtr>> TreeViewPtr;

	/** All Tree view objects */
	TArray<FObjectMixerEditorListRowPtr> TreeViewRootObjects;
	/** Visible Tree view objects, after filters */
	TArray<FObjectMixerEditorListRowPtr> VisibleTreeViewObjects;

	TArray<FListViewColumnInfo> ListViewColumns;

	// Sorting

	FName ActiveSortingColumnName = NAME_None;
	EColumnSortMode::Type ActiveSortingType = EColumnSortMode::None;

	TFunctionRef<bool(const FObjectMixerEditorListRowPtr&, const FObjectMixerEditorListRowPtr&)> SortByOrderAscending =
		[](const FObjectMixerEditorListRowPtr& A, const FObjectMixerEditorListRowPtr& B)
		{
			return A->GetSortOrder() < B->GetSortOrder();
		};

	FDelegateHandle OnActorSpawnedHandle;
	FDelegateHandle OnActorDestroyedHandle;
};
