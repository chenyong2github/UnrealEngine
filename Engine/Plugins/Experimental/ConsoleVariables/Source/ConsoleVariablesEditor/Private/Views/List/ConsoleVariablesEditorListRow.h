// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConsoleVariablesAsset.h"
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
	
	FConsoleVariablesEditorListRow(const FConsoleVariablesUiCommandInfo InCommandInfo, const FText& InSource, const EConsoleVariablesEditorListRowType InRowType, 
		const ECheckBoxState StartingWidgetCheckboxState, const TSharedRef<SConsoleVariablesEditorList>& InListView, 
		const TWeakPtr<FConsoleVariablesEditorListRow>& InDirectParentRow)
	: CommandInfo(InCommandInfo)
	, Source(InSource)
	, RowType(InRowType)
	, WidgetCheckedState(StartingWidgetCheckboxState)
	, ListViewPtr(InListView)
	, DirectParentRow(InDirectParentRow)
	{};

	FConsoleVariablesUiCommandInfo& GetCommandInfo();

	EConsoleVariablesEditorListRowType GetRowType() const;

	int32 GetChildDepth() const;

	void SetChildDepth(const int32 InDepth);

	TWeakPtr<FConsoleVariablesEditorListRow> GetDirectParentRow() const;
	void SetDirectParentRow(const TWeakPtr<FConsoleVariablesEditorListRow>& InDirectParentRow);
	
	/* bHasGeneratedChildren must be true to get actual children. */
	const TArray<FConsoleVariablesEditorListRowPtr>& GetChildRows() const;
	/* bHasGeneratedChildren must be true to get an accurate value. */
	int32 GetChildCount() const;
	void SetChildRows(const TArray<FConsoleVariablesEditorListRowPtr>& InChildRows);
	void AddToChildRows(const FConsoleVariablesEditorListRowPtr& InRow);
	void InsertChildRowAtIndex(const FConsoleVariablesEditorListRowPtr& InRow, const int32 AtIndex = 0);

	bool GetIsTreeViewItemExpanded() const;
	void SetIsTreeViewItemExpanded(const bool bNewExpanded);

	bool GetShouldExpandAllChildren() const;
	void SetShouldExpandAllChildren(const bool bNewShouldExpandAllChildren);

	FText GetSource() const;
	void SetSource(const FString& InSource);

	/* If bMatchAnyTokens is false, only nodes that match all terms will be returned. */
	bool MatchSearchTokensToSearchTerms(const TArray<FString> InTokens, const bool bMatchAnyTokens = false);

	/* This overload creates tokens from a string first, then calls ExecuteSearchOnChildNodes(const TArray<FString>& Tokens). */
	void ExecuteSearchOnChildNodes(const FString& SearchString) const;
	void ExecuteSearchOnChildNodes(const TArray<FString>& Tokens) const;

	ECheckBoxState GetWidgetCheckedState() const;
	void SetWidgetCheckedState(const ECheckBoxState NewState, const bool bShouldUpdateHierarchyCheckedStates = false);

	EVisibility GetDesiredVisibility() const;

	TWeakPtr<SConsoleVariablesEditorList> GetListViewPtr() const
	{
		return ListViewPtr;
	}

	FReply OnRemoveButtonClicked();

private:

	FConsoleVariablesUiCommandInfo CommandInfo;
	FText Source;
	EConsoleVariablesEditorListRowType RowType = SingleCommand;
	TArray<FConsoleVariablesEditorListRowPtr> ChildRows;
	bool bIsTreeViewItemExpanded = false;

	int32 ChildDepth = 0;
	
	ECheckBoxState WidgetCheckedState = ECheckBoxState::Checked;

	TWeakPtr<SConsoleVariablesEditorList> ListViewPtr;
	TWeakPtr<FConsoleVariablesEditorListRow> DirectParentRow;

	// Used to expand all children on shift+click.
	bool bShouldExpandAllChildren = false;
};
