// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsoleVariablesEditorListRow.h"

#include "ConsoleVariablesAsset.h"
#include "ConsoleVariablesEditorModule.h"

#include "Algo/AnyOf.h"
#include "Views/List/SConsoleVariablesEditorList.h"

FConsoleVariablesEditorListRow::~FConsoleVariablesEditorListRow()
{
	FlushReferences();
}

void FConsoleVariablesEditorListRow::FlushReferences()
{
	if (ChildRows.Num())
	{
		ChildRows.Empty();
	}
}

TWeakPtr<FConsoleVariablesEditorCommandInfo> FConsoleVariablesEditorListRow::GetCommandInfo() const
{
	return CommandInfo;
}

FConsoleVariablesEditorListRow::EConsoleVariablesEditorListRowType FConsoleVariablesEditorListRow::GetRowType() const
{
	return RowType;
}

int32 FConsoleVariablesEditorListRow::GetChildDepth() const
{
	return ChildDepth;
}

void FConsoleVariablesEditorListRow::SetChildDepth(const int32 InDepth)
{
	ChildDepth = InDepth;
}

TWeakPtr<FConsoleVariablesEditorListRow> FConsoleVariablesEditorListRow::GetDirectParentRow() const
{
	return DirectParentRow;
}

void FConsoleVariablesEditorListRow::SetDirectParentRow(
	const TWeakPtr<FConsoleVariablesEditorListRow>& InDirectParentRow)
{
	DirectParentRow = InDirectParentRow;
}

const TArray<FConsoleVariablesEditorListRowPtr>& FConsoleVariablesEditorListRow::GetChildRows() const
{
	return ChildRows;
}

int32 FConsoleVariablesEditorListRow::GetChildCount() const
{
	return ChildRows.Num();
}

void FConsoleVariablesEditorListRow::SetChildRows(const TArray<FConsoleVariablesEditorListRowPtr>& InChildRows)
{
	ChildRows = InChildRows;
}

void FConsoleVariablesEditorListRow::AddToChildRows(const FConsoleVariablesEditorListRowPtr& InRow)
{
	ChildRows.Add(InRow);
}

void FConsoleVariablesEditorListRow::InsertChildRowAtIndex(const FConsoleVariablesEditorListRowPtr& InRow,
	const int32 AtIndex)
{
	ChildRows.Insert(InRow, AtIndex);
}

bool FConsoleVariablesEditorListRow::GetIsTreeViewItemExpanded() const
{
	return bIsTreeViewItemExpanded;
}

void FConsoleVariablesEditorListRow::SetIsTreeViewItemExpanded(const bool bNewExpanded)
{
	bIsTreeViewItemExpanded = bNewExpanded;
}

bool FConsoleVariablesEditorListRow::GetShouldExpandAllChildren() const
{
	return bShouldExpandAllChildren;
}

void FConsoleVariablesEditorListRow::SetShouldExpandAllChildren(const bool bNewShouldExpandAllChildren)
{
	bShouldExpandAllChildren = bNewShouldExpandAllChildren;
}

bool FConsoleVariablesEditorListRow::MatchSearchTokensToSearchTerms(const TArray<FString> InTokens,
	const bool bMatchAnyTokens)
{
	// If the search is cleared we'll consider the row to pass search
	bool bMatchFound = InTokens.Num() == 0;

	if (!bMatchFound)
	{
		TSharedPtr<FConsoleVariablesEditorCommandInfo> PinnedInfo = CommandInfo.Pin();
		
		const FString SearchTerms = PinnedInfo->Command + PinnedInfo->GetSource().ToString() +
			(PinnedInfo->ConsoleVariablePtr ? PinnedInfo->ConsoleVariablePtr->GetString() + PinnedInfo->ConsoleVariablePtr->GetHelp() : "");

		bMatchFound = Algo::AnyOf(InTokens,
			[&SearchTerms](const FString& Token)
			{
				return SearchTerms.Contains(Token);
			});
	}

	bDoesRowMatchSeachTerms = bMatchFound;

	return bMatchFound;
}

void FConsoleVariablesEditorListRow::ExecuteSearchOnChildNodes(const FString& SearchString) const
{
	TArray<FString> Tokens;

	SearchString.ParseIntoArray(Tokens, TEXT(" "), true);

	ExecuteSearchOnChildNodes(Tokens);
}

void FConsoleVariablesEditorListRow::ExecuteSearchOnChildNodes(const TArray<FString>& Tokens) const
{
	for (const FConsoleVariablesEditorListRowPtr& ChildRow : GetChildRows())
	{
		if (!ensure(ChildRow.IsValid()))
		{
			continue;
		}

		if (ChildRow->GetRowType() == EConsoleVariablesEditorListRowType::CommandGroup)
		{
			if (ChildRow->MatchSearchTokensToSearchTerms(Tokens))
			{
				// If the group name matches then we pass an empty string to search child nodes since we want them all to be visible
				ChildRow->ExecuteSearchOnChildNodes("");
			}
			else
			{
				// Otherwise we iterate over all child nodes to determine which should and should not be visible
				ChildRow->ExecuteSearchOnChildNodes(Tokens);
			}
		}
		else
		{
			ChildRow->MatchSearchTokensToSearchTerms(Tokens);
		}
	}
}

ECheckBoxState FConsoleVariablesEditorListRow::GetWidgetCheckedState() const
{
	return WidgetCheckedState;
}

void FConsoleVariablesEditorListRow::SetWidgetCheckedState(const ECheckBoxState NewState, const bool bShouldUpdateHierarchyCheckedStates)
{
	WidgetCheckedState = NewState;
}

bool FConsoleVariablesEditorListRow::IsRowChecked() const
{
	return GetWidgetCheckedState() == ECheckBoxState::Checked;
}

EVisibility FConsoleVariablesEditorListRow::GetDesiredVisibility() const
{
	return bDoesRowMatchSeachTerms || HasVisibleChildren() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply FConsoleVariablesEditorListRow::OnRemoveButtonClicked()
{
	if (!ensure(ListViewPtr.IsValid()))
	{
		return FReply::Handled();
	}

	GetCommandInfo().Pin()->ExecuteCommand(GetCommandInfo().Pin()->StartupValueAsString);

	const FConsoleVariablesEditorModule& ConsoleVariablesEditorModule = FConsoleVariablesEditorModule::Get();

	const TObjectPtr<UConsoleVariablesAsset> EditableAsset = ConsoleVariablesEditorModule.GetEditingAsset();
	check(EditableAsset);

	EditableAsset->RemoveConsoleVariable(CommandInfo.Pin()->Command);

	ListViewPtr.Pin()->RefreshList();

	return FReply::Handled();
}

void FConsoleVariablesEditorListRow::ResetToPresetValue() const
{
	GetCommandInfo().Pin()->ExecuteCommand(GetPresetValue());
}

bool FConsoleVariablesEditorListRow::GetShouldFlashOnScrollIntoView() const
{
	return bShouldFlashOnScrollIntoView;
}

void FConsoleVariablesEditorListRow::SetShouldFlashOnScrollIntoView(const bool bNewShouldFlashOnScrollIntoView)
{
	bShouldFlashOnScrollIntoView = bNewShouldFlashOnScrollIntoView;
}

const FString& FConsoleVariablesEditorListRow::GetPresetValue() const
{
	return PresetValue;
}
