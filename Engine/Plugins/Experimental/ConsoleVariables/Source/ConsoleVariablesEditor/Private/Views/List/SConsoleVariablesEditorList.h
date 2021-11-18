// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ConsoleVariablesEditorListRow.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STreeView.h"

class UConsoleVariablesAsset;
class SBox;
class SSearchBox;
class SHeaderRow;

struct FConsoleVariablesEditorListSplitterManager;
typedef TSharedPtr<FConsoleVariablesEditorListSplitterManager> FConsoleVariablesEditorListSplitterManagerPtr;

struct FConsoleVariablesEditorListSplitterManager
{
	float NestedColumnWidth = 0.5f; // The right side of the first splitter which contains the nested splitter for the property widgets
	float SnapshotPropertyColumnWidth = 0.5f;
};

enum class EConsoleVariablesEditorSortType
{
	SortByVariableName,
	SortBySource
};

class SConsoleVariablesEditorList final : public SCompoundWidget
{

public:

	SLATE_BEGIN_ARGS(SConsoleVariablesEditorList)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual ~SConsoleVariablesEditorList() override;

	FMenuBuilder BuildShowOptionsMenu();
	
	void FlushMemory(const bool bShouldKeepMemoryAllocated);

	void RefreshScroll() const;

	void RefreshList(const FString& InConsoleCommandToScrollTo = "");

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

	// Column Names

	static const FName CheckBoxColumnName;
	static const FName VariableNameColumnName;
	static const FName ValueColumnName;
	static const FName SourceColumnName;

private:
	
	TSharedPtr<SHeaderRow> HeaderRow;
	TSharedPtr<SHeaderRow> GenerateHeaderRow();
	ECheckBoxState HeaderCheckBoxState = ECheckBoxState::Checked;
	
	FReply SetAllGroupsCollapsed();

	// Search
	
	void OnListViewSearchTextChanged(const FText& Text) const;

	TSharedPtr<SSearchBox> ListSearchBoxPtr;
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

	TFunctionRef<bool(const FConsoleVariablesEditorListRowPtr&, const FConsoleVariablesEditorListRowPtr&)> SortBySourceAscending =
		[](const FConsoleVariablesEditorListRowPtr& A, const FConsoleVariablesEditorListRowPtr& B)
	{
		return A->GetCommandInfo().Pin()->GetSource().ToString() < B->GetCommandInfo().Pin()->GetSource().ToString();
	};

	TFunctionRef<bool(const FConsoleVariablesEditorListRowPtr&, const FConsoleVariablesEditorListRowPtr&)> SortBySourceDescending =
		[](const FConsoleVariablesEditorListRowPtr& A, const FConsoleVariablesEditorListRowPtr& B)
	{
		return B->GetCommandInfo().Pin()->GetSource().ToString() < A->GetCommandInfo().Pin()->GetSource().ToString();
	};
};
