// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConsoleVariablesEditorListRow.h"

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

FConsoleVariablesUiCommandInfo& FConsoleVariablesEditorListRow::GetCommandInfo()
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

FText FConsoleVariablesEditorListRow::GetSource() const
{
	return Source;
}

void FConsoleVariablesEditorListRow::SetSource(const FString& InSource)
{
	Source = FText::FromString(InSource);
}

bool FConsoleVariablesEditorListRow::MatchSearchTokensToSearchTerms(const TArray<FString> InTokens,
	const bool bMatchAnyTokens)
{
	return true;
}

void FConsoleVariablesEditorListRow::ExecuteSearchOnChildNodes(const FString& SearchString) const
{
	return;
}

void FConsoleVariablesEditorListRow::ExecuteSearchOnChildNodes(const TArray<FString>& Tokens) const
{
	return;
}

ECheckBoxState FConsoleVariablesEditorListRow::GetWidgetCheckedState() const
{
	return WidgetCheckedState;
}

void FConsoleVariablesEditorListRow::SetWidgetCheckedState(const ECheckBoxState NewState, const bool bShouldUpdateHierarchyCheckedStates)
{
	WidgetCheckedState = NewState;

	if (const TSharedPtr<SConsoleVariablesEditorList>& PinnedList = ListViewPtr.Pin())
	{
		if (bShouldUpdateHierarchyCheckedStates && GetRowType() == HeaderRow && NewState != ECheckBoxState::Undetermined)
		{
			PinnedList->SetAllListViewItemsCheckState(NewState);
		}
		else if (GetRowType() != HeaderRow)
		{
			if (PinnedList->DoesListHaveCheckedMembers())
			{
				if (PinnedList->DoesListHaveUncheckedMembers())
				{
					PinnedList->GetHeaderRow()->SetWidgetCheckedState(ECheckBoxState::Undetermined, false);
				}
				else
				{
					PinnedList->GetHeaderRow()->SetWidgetCheckedState(ECheckBoxState::Checked, false);
				}
			}
			else
			{
				PinnedList->GetHeaderRow()->SetWidgetCheckedState(ECheckBoxState::Unchecked, false);
			}
		}
	}
}

EVisibility FConsoleVariablesEditorListRow::GetDesiredVisibility() const
{
	return EVisibility::Visible;
}

FReply FConsoleVariablesEditorListRow::OnRemoveButtonClicked()
{
	if (!ensure(ListViewPtr.IsValid()))
	{
		return FReply::Handled();
	}

	TWeakObjectPtr<UConsoleVariablesAsset> Asset = ListViewPtr.Pin()->GetEditedAsset();

	if (ensure(Asset.IsValid()))
	{
		Asset->RemoveConsoleVariable(CommandInfo);

		ListViewPtr.Pin()->RefreshList(Asset.Get());
	}

	return FReply::Handled();
}
