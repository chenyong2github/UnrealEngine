// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConsoleVariablesEditorCommandInfo.h"
#include "Widgets/Input/SCheckBox.h"

class SConsoleVariablesEditorList;

struct FConsoleVariablesEditorListRow;
typedef TSharedPtr<FConsoleVariablesEditorListRow> FConsoleVariablesEditorListRowPtr;

struct FConsoleVariablesEditorListRow final : TSharedFromThis<FConsoleVariablesEditorListRow>
{
	enum EConsoleVariablesEditorListRowType
	{
		None,
		HeaderRow, 
		CommandGroup, // Group of commands or subgroups
		SingleCommand
	};

	~FConsoleVariablesEditorListRow();

	void FlushReferences();
	
	FConsoleVariablesEditorListRow(
		const TWeakPtr<FConsoleVariablesEditorCommandInfo> InCommandInfo, const FString& InPresetValue, const EConsoleVariablesEditorListRowType InRowType, 
		const ECheckBoxState StartingWidgetCheckboxState, const TSharedRef<SConsoleVariablesEditorList>& InListView, 
		const int32 IndexInList, const TWeakPtr<FConsoleVariablesEditorListRow>& InDirectParentRow)
	: CommandInfo(InCommandInfo)
	, PresetValue(InPresetValue)
	, RowType(InRowType)
	, WidgetCheckedState(StartingWidgetCheckboxState)
	, ListViewPtr(InListView)
	, SortOrder(IndexInList)
	, DirectParentRow(InDirectParentRow)
	{}

	[[nodiscard]] TWeakPtr<FConsoleVariablesEditorCommandInfo> GetCommandInfo() const;

	[[nodiscard]] EConsoleVariablesEditorListRowType GetRowType() const;

	[[nodiscard]] int32 GetChildDepth() const;
	void SetChildDepth(const int32 InDepth);

	[[nodiscard]] int32 GetSortOrder() const;
	void SetSortOrder(const int32 InNewOrder);

	TWeakPtr<FConsoleVariablesEditorListRow> GetDirectParentRow() const;
	void SetDirectParentRow(const TWeakPtr<FConsoleVariablesEditorListRow>& InDirectParentRow);
	
	/* bHasGeneratedChildren must be true to get actual children. */
	[[nodiscard]] const TArray<FConsoleVariablesEditorListRowPtr>& GetChildRows() const;
	/* bHasGeneratedChildren must be true to get an accurate value. */
	[[nodiscard]] int32 GetChildCount() const;
	void SetChildRows(const TArray<FConsoleVariablesEditorListRowPtr>& InChildRows);
	void AddToChildRows(const FConsoleVariablesEditorListRowPtr& InRow);
	void InsertChildRowAtIndex(const FConsoleVariablesEditorListRowPtr& InRow, const int32 AtIndex = 0);

	[[nodiscard]] bool GetIsTreeViewItemExpanded() const;
	void SetIsTreeViewItemExpanded(const bool bNewExpanded);
	
	[[nodiscard]] bool GetShouldFlashOnScrollIntoView() const;
	void SetShouldFlashOnScrollIntoView(const bool bNewShouldFlashOnScrollIntoView);

	[[nodiscard]] bool GetShouldExpandAllChildren() const;
	void SetShouldExpandAllChildren(const bool bNewShouldExpandAllChildren);

	void ResetToStartupValueAndSource() const;
	
	[[nodiscard]] const FString& GetPresetValue() const;

	/* If bMatchAnyTokens is false, only nodes that match all terms will be returned. */
	bool MatchSearchTokensToSearchTerms(const TArray<FString> InTokens, const bool bMatchAnyTokens = false);

	/* This overload creates tokens from a string first, then calls ExecuteSearchOnChildNodes(const TArray<FString>& Tokens). */
	void ExecuteSearchOnChildNodes(const FString& SearchString) const;
	void ExecuteSearchOnChildNodes(const TArray<FString>& Tokens) const;

	[[nodiscard]] bool GetDoesRowPassFilters() const;
	void SetDoesRowPassFilters(const bool bPass);

	[[nodiscard]] bool GetIsSelected() const;
	void SetIsSelected(const bool bNewSelected);

	[[nodiscard]] ECheckBoxState GetWidgetCheckedState() const;
	void SetWidgetCheckedState(const ECheckBoxState NewState, const bool bShouldUpdateHierarchyCheckedStates = false);

	[[nodiscard]] bool IsRowChecked() const;

	[[nodiscard]] EVisibility GetDesiredVisibility() const;

	[[nodiscard]] bool HasVisibleChildren() const
	{
		return false;
	}

	[[nodiscard]] TWeakPtr<SConsoleVariablesEditorList> GetListViewPtr() const
	{
		return ListViewPtr;
	}

	[[nodiscard]] TArray<FConsoleVariablesEditorListRowPtr> GetSelectedTreeViewItems() const;

	FReply OnRemoveButtonClicked();
	
	void ResetToPresetValue() const;

private:

	TWeakPtr<FConsoleVariablesEditorCommandInfo> CommandInfo;
	FString PresetValue = "";
	EConsoleVariablesEditorListRowType RowType = SingleCommand;
	TArray<FConsoleVariablesEditorListRowPtr> ChildRows;

	ECheckBoxState WidgetCheckedState = ECheckBoxState::Checked;
	
	TWeakPtr<SConsoleVariablesEditorList> ListViewPtr;
	
	bool bIsTreeViewItemExpanded = false;
	bool bShouldFlashOnScrollIntoView = false;

	int32 ChildDepth = 0;

	int32 SortOrder = -1;

	bool bDoesRowMatchSearchTerms = true;
	bool bDoesRowPassFilters = true;

	bool bIsSelected = false;
	TWeakPtr<FConsoleVariablesEditorListRow> DirectParentRow;

	// Used to expand all children on shift+click.
	bool bShouldExpandAllChildren = false;
};
