// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ConsoleVariablesEditorListRow.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

class SBox;
class SSearchBox;

struct FConsoleVariablesEditorListSplitterManager;
typedef TSharedPtr<FConsoleVariablesEditorListSplitterManager> FConsoleVariablesEditorListSplitterManagerPtr;

struct FConsoleVariablesEditorListSplitterManager
{
	float NestedColumnWidth = 0.5f; // The right side of the first splitter which contains the nested splitter for the property widgets
	float SnapshotPropertyColumnWidth = 0.5f;
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

	void RefreshList(UConsoleVariablesAsset* InAsset);

	/** Iterates through existing list and updates all values to current ones without executing the commands themselves. */
	void UpdateExistingValuesFromConsoleManager();

	/** Iterates through existing list and sets corresponding values on the asset being edited so changed can be saved. */
	void PropagateRowValueChangesBackToEditingAsset();

	FString GetSearchStringFromSearchInputField() const;
	void ExecuteListViewSearchOnAllActors(const FString& SearchString) const;

	bool DoesTreeViewHaveVisibleChildren() const;

	void SetTreeViewItemExpanded(const TSharedPtr<FConsoleVariablesEditorListRow>& RowToExpand, const bool bNewExpansion) const;

	void SetAllListViewItemsCheckState(const ECheckBoxState InNewState);

	bool DoesListHaveCheckedMembers() const;

	bool DoesListHaveUncheckedMembers() const;

	FConsoleVariablesEditorListRowPtr& GetHeaderRow()
	{
		return HeaderRow;
	}

	TWeakObjectPtr<UConsoleVariablesAsset> GetEditedAsset() const
	{
		return EditedAsset;
	}

private:

	FText DefaultNameText;
		
	FReply SetAllGroupsCollapsed();

	// Search
	
	void OnListViewSearchTextChanged(const FText& Text) const;

	TSharedPtr<SSearchBox> ListSearchBoxPtr;
	TSharedPtr<SBox> ListBoxContainerPtr;

	/* For splitter sync */
	FConsoleVariablesEditorListSplitterManagerPtr SplitterManagerPtr;

	//  Tree View Implementation

	void GenerateTreeView(UConsoleVariablesAsset* InAsset);

	TWeakObjectPtr<UConsoleVariablesAsset> EditedAsset;
	
	void OnGetRowChildren(FConsoleVariablesEditorListRowPtr Row, TArray<FConsoleVariablesEditorListRowPtr>& OutChildren) const;
	void OnRowChildExpansionChange(FConsoleVariablesEditorListRowPtr Row, const bool bIsExpanded, const bool bIsRecursive = false) const;

	void SetChildExpansionRecursively(const FConsoleVariablesEditorListRowPtr& InRow, const bool bNewIsExpanded) const;

	TSharedPtr<SBox> HeaderBoxPtr;
	FConsoleVariablesEditorListRowPtr HeaderRow;
	TSharedPtr<STreeView<FConsoleVariablesEditorListRowPtr>> TreeViewPtr;
	
	TArray<FConsoleVariablesEditorListRowPtr> TreeViewRootObjects;
};