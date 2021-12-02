// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ConsoleVariablesEditorListRow.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STreeView.h"

class UConsoleVariablesAsset;
class SBox;
class SComboButton;
class SSearchBox;
class SHeaderRow;

class SConsoleVariablesEditorList final : public SCompoundWidget
{

public:

	SLATE_BEGIN_ARGS(SConsoleVariablesEditorList)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual ~SConsoleVariablesEditorList() override;

	TSharedRef<SWidget> BuildShowOptionsMenu();
	
	void FlushMemory(const bool bShouldKeepMemoryAllocated);

	void RefreshScroll() const;

	void RefreshList(const FString& InConsoleCommandToScrollTo = "");

	[[nodiscard]] TArray<FConsoleVariablesEditorListRowPtr> GetSelectedTreeViewItems() const;

	[[nodiscard]] TArray<FConsoleVariablesEditorListRowPtr> GetTreeViewItems() const;
	void SetTreeViewItems(const TArray<FConsoleVariablesEditorListRowPtr>& InItems);

	[[nodiscard]] int32 GetTreeViewItemCount() const
	{
		return TreeViewRootObjects.Num();
	}

	/** Updates the saved values in a UConsoleVariablesAsset so that the command/value map can be saved to disk */
	void UpdatePresetValuesForSave(TObjectPtr<UConsoleVariablesAsset> InAsset) const;

	FString GetSearchStringFromSearchInputField() const;
	void ExecuteListViewSearchOnAllRows(const FString& SearchString) const;

	bool DoesTreeViewHaveVisibleChildren() const;

	void SetTreeViewItemExpanded(const TSharedPtr<FConsoleVariablesEditorListRow>& RowToExpand, const bool bNewExpansion) const;

	void SetAllListViewItemsCheckState(const ECheckBoxState InNewState);

	bool DoesListHaveCheckedMembers() const;

	bool DoesListHaveUncheckedMembers() const;
	
	void OnListItemCheckBoxStateChange(const ECheckBoxState InNewState);

	// Sorting
	
	EColumnSortMode::Type GetSortMode(FName InColumnName) const;
	void OnSortColumnCalled(EColumnSortPriority::Type Priority, const FName& ColumnName, EColumnSortMode::Type SortMode);
	EColumnSortMode::Type CycleSortMode(const FName& InColumnName);
	void ExecuteSort(const FName& InColumnName, const EColumnSortMode::Type InColumnSortMode);
	void ClearSorting()
	{
		SortingMap.Empty();
	}

	// Column Names

	static const FName CustomSortOrderColumnName;
	static const FName CheckBoxColumnName;
	static const FName VariableNameColumnName;
	static const FName ValueColumnName;
	static const FName SourceColumnName;

private:
	
	TSharedPtr<SHeaderRow> HeaderRow;
	TSharedPtr<SHeaderRow> GenerateHeaderRow();
	ECheckBoxState HeaderCheckBoxState = ECheckBoxState::Checked;
	
	void SetAllGroupsCollapsed();
	void SetSortOrder();

	// Search
	
	void OnListViewSearchTextChanged(const FText& Text) const;

	TSharedPtr<SSearchBox> ListSearchBoxPtr;
	TSharedPtr<SComboButton> ViewOptionsComboButton;
	TSharedPtr<SBox> ListBoxContainerPtr;

	//  Tree View Implementation

	void GenerateTreeView();
	
	void OnGetRowChildren(FConsoleVariablesEditorListRowPtr Row, TArray<FConsoleVariablesEditorListRowPtr>& OutChildren) const;
	void OnRowChildExpansionChange(FConsoleVariablesEditorListRowPtr Row, const bool bIsExpanded, const bool bIsRecursive = false) const;

	void SetChildExpansionRecursively(const FConsoleVariablesEditorListRowPtr& InRow, const bool bNewIsExpanded) const;

	TSharedPtr<STreeView<FConsoleVariablesEditorListRowPtr>> TreeViewPtr;
	
	TArray<FConsoleVariablesEditorListRowPtr> TreeViewRootObjects;

	// Sorting
	
	TMap<FName, EColumnSortMode::Type> SortingMap;

	TFunctionRef<bool(const FConsoleVariablesEditorListRowPtr&, const FConsoleVariablesEditorListRowPtr&)> SortByOrderAscending =
		[](const FConsoleVariablesEditorListRowPtr& A, const FConsoleVariablesEditorListRowPtr& B)
		{
			return A->GetSortOrder() < B->GetSortOrder();
		};

	TFunctionRef<bool(const FConsoleVariablesEditorListRowPtr&, const FConsoleVariablesEditorListRowPtr&)> SortByOrderDescending =
		[](const FConsoleVariablesEditorListRowPtr& A, const FConsoleVariablesEditorListRowPtr& B)
		{
			return B->GetSortOrder() < A->GetSortOrder();
		};

	TFunctionRef<bool(const FConsoleVariablesEditorListRowPtr&, const FConsoleVariablesEditorListRowPtr&)> SortBySourceAscending =
		[](const FConsoleVariablesEditorListRowPtr& A, const FConsoleVariablesEditorListRowPtr& B)
		{
			return A->GetCommandInfo().Pin()->GetSourceAsText().ToString() < B->GetCommandInfo().Pin()->GetSourceAsText().ToString();
		};

	TFunctionRef<bool(const FConsoleVariablesEditorListRowPtr&, const FConsoleVariablesEditorListRowPtr&)> SortBySourceDescending =
		[](const FConsoleVariablesEditorListRowPtr& A, const FConsoleVariablesEditorListRowPtr& B)
		{
			return B->GetCommandInfo().Pin()->GetSourceAsText().ToString() < A->GetCommandInfo().Pin()->GetSourceAsText().ToString();
		};
	
	TFunctionRef<bool(const FConsoleVariablesEditorListRowPtr&, const FConsoleVariablesEditorListRowPtr&)> SortByVariableNameAscending =
		[](const FConsoleVariablesEditorListRowPtr& A, const FConsoleVariablesEditorListRowPtr& B)
	{
		return A->GetCommandInfo().Pin()->Command < B->GetCommandInfo().Pin()->Command;
	};

	TFunctionRef<bool(const FConsoleVariablesEditorListRowPtr&, const FConsoleVariablesEditorListRowPtr&)> SortByVariableNameDescending =
		[](const FConsoleVariablesEditorListRowPtr& A, const FConsoleVariablesEditorListRowPtr& B)
	{
		return B->GetCommandInfo().Pin()->Command < A->GetCommandInfo().Pin()->Command;
	};
};
