// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConsoleVariablesEditorList.h"

#include "ConsoleVariablesAsset.h"
#include "ConsoleVariablesEditorModule.h"
#include "SConsoleVariablesEditorListRow.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

#define LOCTEXT_NAMESPACE "ConsoleVariablesEditor"

const FName SConsoleVariablesEditorList::CheckBoxColumnName(TEXT("Column"));
const FName SConsoleVariablesEditorList::VariableNameColumnName(TEXT("Name"));
const FName SConsoleVariablesEditorList::ValueColumnName(TEXT("Value"));
const FName SConsoleVariablesEditorList::SourceColumnName(TEXT("Source"));

void SConsoleVariablesEditorList::Construct(const FArguments& InArgs)
{
	HeaderRow = SNew(SHeaderRow)
				.CanSelectGeneratedColumn(true).Visibility(EVisibility::Visible);
	
	GenerateHeaderRow();
	
	ChildSlot
	[
		SNew(SVerticalBox)
		
		+ SVerticalBox::Slot()
		.VAlign(VAlign_Top)
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			+SHorizontalBox::Slot()
			[
				SAssignNew(ListSearchBoxPtr, SSearchBox)
				.HintText(LOCTEXT("ConsoleVariablesEditorList_SearchHintText", "Search tracked variables, values, sources or help text..."))
				.OnTextChanged_Raw(this, &SConsoleVariablesEditorList::OnListViewSearchTextChanged)
			]
		]

		+ SVerticalBox::Slot()
		[
			SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda([this]()
			{
				return DoesTreeViewHaveVisibleChildren() ? 0 : 1;
			})
			
			+ SWidgetSwitcher::Slot()
			.HAlign(HAlign_Fill)
			.Padding(2.0f, 2.0f, 2.0f, 2.0f)
			[
				SAssignNew(TreeViewPtr, STreeView<FConsoleVariablesEditorListRowPtr>)
				.HeaderRow(HeaderRow)
				.SelectionMode(ESelectionMode::None)
				.TreeItemsSource(&TreeViewRootObjects)
				.OnGenerateRow_Lambda([this](FConsoleVariablesEditorListRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable)
					{
						check(Row.IsValid());
					
						return SNew(SConsoleVariablesEditorListRow, TreeViewPtr.ToSharedRef(), Row)
								.Visibility_Raw(Row.Get(), &FConsoleVariablesEditorListRow::GetDesiredVisibility);
					})
				.OnGetChildren_Raw(this, &SConsoleVariablesEditorList::OnGetRowChildren)
				.OnExpansionChanged_Raw(this, &SConsoleVariablesEditorList::OnRowChildExpansionChange, false)
				.OnSetExpansionRecursive(this, &SConsoleVariablesEditorList::OnRowChildExpansionChange, true)
			]

			// For when no rows exist in view
			+ SWidgetSwitcher::Slot()
			.HAlign(HAlign_Center)
			.Padding(2.0f, 24.0f, 2.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ConsoleVariablesEditorList_NoList", "No List to show. Try clearing the active search or adding some console variables to the list."))
			]
		]
	];
}

SConsoleVariablesEditorList::~SConsoleVariablesEditorList()
{	
	ListSearchBoxPtr.Reset();
	ListBoxContainerPtr.Reset();

	FlushMemory(false);

	TreeViewPtr.Reset();
}

FMenuBuilder SConsoleVariablesEditorList::BuildShowOptionsMenu()
{
	FMenuBuilder ShowOptionsMenuBuilder = FMenuBuilder(true, nullptr);

	ShowOptionsMenuBuilder.AddMenuEntry(
		LOCTEXT("CollapseAll", "Collapse All"),
		LOCTEXT("ConsoleVariablesEditorList_CollapseAll_Tooltip", "Collapse all expanded actor groups in the Modified Actors list."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() {
				SetAllGroupsCollapsed();
				})
		),
		NAME_None,
		EUserInterfaceActionType::Button
	);

	return ShowOptionsMenuBuilder;
}

void SConsoleVariablesEditorList::FlushMemory(const bool bShouldKeepMemoryAllocated)
{
	if (bShouldKeepMemoryAllocated)
	{
		TreeViewRootObjects.Reset();
	}
	else
	{
		TreeViewRootObjects.Empty();
	}
}

void SConsoleVariablesEditorList::RefreshScroll() const
{
	TreeViewPtr->RequestListRefresh();
}

void SConsoleVariablesEditorList::RefreshList(const FString& InConsoleCommandToScrollTo)
{
	GenerateTreeView();

	// Enforce Sort
	TArray<FName> MapKeys;
	if (SortingMap.GetKeys(MapKeys))
	{
		const FName& Key = MapKeys[0];
		const EColumnSortMode::Type Mode = *SortingMap.Find(Key);
		
		ExecuteSort(Key, Mode);
	}

	if (!InConsoleCommandToScrollTo.IsEmpty())
	{
		FConsoleVariablesEditorListRowPtr ScrollToItem = nullptr;

		for (const FConsoleVariablesEditorListRowPtr& Item : TreeViewRootObjects)
		{
			if (Item->GetCommandInfo().Pin()->Command.Equals(InConsoleCommandToScrollTo))
			{
				ScrollToItem = Item;
				break;
			}
		}

		if (ScrollToItem.IsValid())
		{
			ScrollToItem->SetShouldFlashOnScrollIntoView(true);
			TreeViewPtr->RequestScrollIntoView(ScrollToItem);
		}
	}
}

void SConsoleVariablesEditorList::UpdatePresetValuesForSave(TObjectPtr<UConsoleVariablesAsset> InAsset) const
{
	TMap<FString, FString> NewSavedValueMap;
	
	for (const FConsoleVariablesEditorListRowPtr& Item : TreeViewRootObjects)
	{
		const TWeakPtr<FConsoleVariablesEditorCommandInfo> CommandInfo = Item->GetCommandInfo();
		
		if (CommandInfo.IsValid())
		{
			if (const TObjectPtr<IConsoleVariable> Variable = CommandInfo.Pin()->ConsoleVariablePtr)
			{
				NewSavedValueMap.Add(CommandInfo.Pin()->Command, Variable->GetString());
			}
		}
	}

	InAsset->ReplaceSavedCommandsAndValues(NewSavedValueMap);
}

FString SConsoleVariablesEditorList::GetSearchStringFromSearchInputField() const
{
	return ensureAlwaysMsgf(ListSearchBoxPtr.IsValid(), TEXT("%hs: ListSearchBoxPtr is not valid. Check to make sure it was created."), __FUNCTION__)
	? ListSearchBoxPtr->GetText().ToString() : "";
}

void SConsoleVariablesEditorList::GenerateTreeView()
{	
	if (!ensure(TreeViewPtr.IsValid()))
	{
		return;
	}
	
	FlushMemory(true);

	FConsoleVariablesEditorModule& ConsoleVariablesEditorModule = FConsoleVariablesEditorModule::Get();

	const TObjectPtr<UConsoleVariablesAsset> EditableAsset = ConsoleVariablesEditorModule.GetEditingAsset();
	check(EditableAsset);

	for (const TPair<FString, FString>& CommandAndValue : EditableAsset->GetSavedCommandsAndValues())
	{
		TWeakPtr<FConsoleVariablesEditorCommandInfo> CommandInfo = ConsoleVariablesEditorModule.FindCommandInfoByName(CommandAndValue.Key);
		
		if (CommandInfo.IsValid())
		{
			if (const TObjectPtr<IConsoleVariable> VariablePtr = CommandInfo.Pin()->ConsoleVariablePtr)
			{
				if (!VariablePtr->GetString().Equals(CommandAndValue.Value))
				{
					CommandInfo.Pin()->ExecuteCommand(CommandAndValue.Value);
				}
			
				FConsoleVariablesEditorListRowPtr NewRow = 
					MakeShared<FConsoleVariablesEditorListRow>(
							CommandInfo.Pin(), CommandAndValue.Value, FConsoleVariablesEditorListRow::SingleCommand, 
							ECheckBoxState::Checked, SharedThis(this), nullptr);
				TreeViewRootObjects.Add(NewRow);
			}
		}
	}

	if (TreeViewRootObjects.Num() > 0)
	{
		TreeViewPtr->RequestListRefresh();

		// Apply last search
		ExecuteListViewSearchOnAllRows(GetSearchStringFromSearchInputField());
	}
}

TSharedPtr<SHeaderRow> SConsoleVariablesEditorList::GenerateHeaderRow()
{
	check(HeaderRow);
	HeaderRow->ClearColumns();
	
	HeaderRow->AddColumn(
		SHeaderRow::Column(CheckBoxColumnName)
			.DefaultLabel(LOCTEXT("ConsoleVariablesEditorList_ConsoleVariableCheckboxHeaderText", "Checkbox"))
			.HAlignHeader(EHorizontalAlignment::HAlign_Center)
			.FixedWidth(50.f)
			.ShouldGenerateWidget(true)
			.HeaderContent()
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]()
				{
					return HeaderCheckBoxState;
				})
				.OnCheckStateChanged_Lambda([this] (const ECheckBoxState NewState)
				{
					HeaderCheckBoxState = NewState;
					
					for (const FConsoleVariablesEditorListRowPtr& Object : TreeViewRootObjects)
					{
						Object->SetWidgetCheckedState(NewState);
					}
				})
			]
	);

	HeaderRow->AddColumn(
		SHeaderRow::Column(VariableNameColumnName)
			.DefaultLabel(LOCTEXT("ConsoleVariablesEditorList_ConsoleVariableNameHeaderText", "Console Variable Name"))
			.HAlignHeader(EHorizontalAlignment::HAlign_Left)
			.ShouldGenerateWidget(true)
			.SortMode_Raw(this, &SConsoleVariablesEditorList::GetSortMode, VariableNameColumnName)
			.OnSort_Raw(this, &SConsoleVariablesEditorList::OnSortColumnCalled)
	);

	HeaderRow->AddColumn(
		SHeaderRow::Column(ValueColumnName)
			.DefaultLabel(LOCTEXT("ConsoleVariablesEditorList_ConsoleVariableValueHeaderText", "Value"))
			.HAlignHeader(EHorizontalAlignment::HAlign_Left)
			.ShouldGenerateWidget(true)
	);

	HeaderRow->AddColumn(
		SHeaderRow::Column(SourceColumnName)
			.DefaultLabel(LOCTEXT("ConsoleVariablesEditorList_SourceHeaderText", "Source"))
			.HAlignHeader(EHorizontalAlignment::HAlign_Left)
			.SortMode_Raw(this, &SConsoleVariablesEditorList::GetSortMode, SourceColumnName)
			.OnSort_Raw(this, &SConsoleVariablesEditorList::OnSortColumnCalled)
	);

	return HeaderRow;
}

FReply SConsoleVariablesEditorList::SetAllGroupsCollapsed()
{
	if (TreeViewPtr.IsValid())
	{
		for (const FConsoleVariablesEditorListRowPtr& RootRow : TreeViewRootObjects)
		{
			if (!RootRow.IsValid())
			{
				continue;
			}
			
			TreeViewPtr->SetItemExpansion(RootRow, false);
			RootRow->SetIsTreeViewItemExpanded(false);
		}
	}

	return FReply::Handled();
}

void SConsoleVariablesEditorList::OnListViewSearchTextChanged(const FText& Text) const
{
	ExecuteListViewSearchOnAllRows(Text.ToString());
}

void SConsoleVariablesEditorList::ExecuteListViewSearchOnAllRows(const FString& SearchString) const
{
	TArray<FString> Tokens;
	
	// unquoted search equivalent to a match-any-of search
	SearchString.ParseIntoArray(Tokens, TEXT(" "), true);
	
	for (const TSharedPtr<FConsoleVariablesEditorListRow>& ChildRow : TreeViewRootObjects)
	{
		if (!ensure(ChildRow.IsValid()))
		{
			continue;
		}
		
		const bool bGroupMatch = ChildRow->MatchSearchTokensToSearchTerms(Tokens);
		
		// If the group name matches then we pass in an empty string so all child nodes are visible.
		// If the name doesn't match, then we need to evaluate each child.
		ChildRow->ExecuteSearchOnChildNodes(bGroupMatch ? "" : SearchString);
	}

	TreeViewPtr->RequestTreeRefresh();
}

bool SConsoleVariablesEditorList::DoesTreeViewHaveVisibleChildren() const
{
	if (TreeViewPtr.IsValid())
	{
		for (const TSharedPtr<FConsoleVariablesEditorListRow>& Header : TreeViewRootObjects)
		{
			const EVisibility HeaderVisibility = Header->GetDesiredVisibility();
			
			if (HeaderVisibility != EVisibility::Hidden && HeaderVisibility != EVisibility::Collapsed)
			{
				return true;
			}
		}
	}
	
	return false;
}

void SConsoleVariablesEditorList::SetTreeViewItemExpanded(const TSharedPtr<FConsoleVariablesEditorListRow>& RowToExpand, const bool bNewExpansion) const
{
	if (TreeViewPtr.IsValid())
	{
		TreeViewPtr->SetItemExpansion(RowToExpand, bNewExpansion);
	}
}

void SConsoleVariablesEditorList::SetAllListViewItemsCheckState(const ECheckBoxState InNewState)
{
	for (const TSharedPtr<FConsoleVariablesEditorListRow>& Row : TreeViewRootObjects)
	{
		Row->SetWidgetCheckedState(InNewState);
	}
}

bool SConsoleVariablesEditorList::DoesListHaveCheckedMembers() const
{
	for (const TSharedPtr<FConsoleVariablesEditorListRow>& Row : TreeViewRootObjects)
	{
		if (Row->GetWidgetCheckedState() == ECheckBoxState::Checked)
		{
			return true;
		}
	}

	return false;
}

bool SConsoleVariablesEditorList::DoesListHaveUncheckedMembers() const
{
	for (const TSharedPtr<FConsoleVariablesEditorListRow>& Row : TreeViewRootObjects)
	{
		if (Row->GetWidgetCheckedState() == ECheckBoxState::Unchecked)
		{
			return true;
		}
	}

	return false;
}

void SConsoleVariablesEditorList::OnListItemCheckBoxStateChange(const ECheckBoxState InNewState)
{
	HeaderCheckBoxState = ECheckBoxState::Checked;

	if (DoesListHaveUncheckedMembers())
	{
		HeaderCheckBoxState = ECheckBoxState::Unchecked;

		if (DoesListHaveCheckedMembers())
		{
			HeaderCheckBoxState = ECheckBoxState::Undetermined;
		}
	}
}

EColumnSortMode::Type SConsoleVariablesEditorList::GetSortMode(FName InColumnName) const
{
	EColumnSortMode::Type ColumnSortMode = EColumnSortMode::None;

	if (const EColumnSortMode::Type* FoundSortMode = SortingMap.Find(InColumnName))
	{
		ColumnSortMode = *FoundSortMode;
	}

	return ColumnSortMode;
}

void SConsoleVariablesEditorList::OnSortColumnCalled(EColumnSortPriority::Type Priority, const FName& ColumnName, EColumnSortMode::Type SortMode)
{
	ExecuteSort(ColumnName, CycleSortMode(ColumnName));
}

EColumnSortMode::Type SConsoleVariablesEditorList::CycleSortMode(const FName& InColumnName)
{
	EColumnSortMode::Type ColumnSortMode = EColumnSortMode::None;

	if (const EColumnSortMode::Type* FoundSortMode = SortingMap.Find(InColumnName))
	{
		ColumnSortMode = *FoundSortMode;
	}

	switch (ColumnSortMode)
	{
	case EColumnSortMode::None:
		ColumnSortMode = EColumnSortMode::Ascending;
		break;

	case EColumnSortMode::Ascending:
		ColumnSortMode = EColumnSortMode::Descending;
		break;

	case EColumnSortMode::Descending:
		ColumnSortMode = EColumnSortMode::None;
		break;

	default:
		ColumnSortMode = EColumnSortMode::None;
		break;
	}

	SortingMap.Empty();
	SortingMap.Add(InColumnName, ColumnSortMode);

	return ColumnSortMode;
}

void SConsoleVariablesEditorList::ExecuteSort(const FName& InColumnName, const EColumnSortMode::Type InColumnSortMode)
{
	if (InColumnSortMode == EColumnSortMode::Ascending)
	{
		if (InColumnName.IsEqual(VariableNameColumnName))
		{
			TreeViewRootObjects.StableSort(SortByVariableNameAscending);
		}
		else if (InColumnName.IsEqual(SourceColumnName))
		{
			TreeViewRootObjects.StableSort(SortBySourceAscending);
		}
	}
	else if (InColumnSortMode == EColumnSortMode::Descending)
	{
		if (InColumnName.IsEqual(VariableNameColumnName))
		{
			TreeViewRootObjects.StableSort(SortByVariableNameDescending);
		}
		else if (InColumnName.IsEqual(SourceColumnName))
		{
			TreeViewRootObjects.StableSort(SortBySourceDescending);
		}
	}
	
	TreeViewPtr->RequestTreeRefresh();
}

void SConsoleVariablesEditorList::OnGetRowChildren(FConsoleVariablesEditorListRowPtr Row, TArray<FConsoleVariablesEditorListRowPtr>& OutChildren) const
{
	if (Row.IsValid())
	{
		OutChildren = Row->GetChildRows();

		if (Row->GetShouldExpandAllChildren())
		{
			SetChildExpansionRecursively(Row, true);
			Row->SetShouldExpandAllChildren(false);
		}
	}
}

void SConsoleVariablesEditorList::OnRowChildExpansionChange(FConsoleVariablesEditorListRowPtr Row, const bool bIsExpanded, const bool bIsRecursive) const
{
	if (Row.IsValid())
	{
		if (bIsRecursive)
		{
			if (bIsExpanded)
			{
				if (Row->GetRowType() != FConsoleVariablesEditorListRow::HeaderRow)
				{
					Row->SetShouldExpandAllChildren(true);
				}
			}
			else
			{
				SetChildExpansionRecursively(Row, bIsExpanded);
			}
		}
		
		TreeViewPtr->SetItemExpansion(Row, bIsExpanded);
		Row->SetIsTreeViewItemExpanded(bIsExpanded);
	}
}

void SConsoleVariablesEditorList::SetChildExpansionRecursively(const FConsoleVariablesEditorListRowPtr& InRow, const bool bNewIsExpanded) const
{
	if (InRow.IsValid())
	{
		for (const FConsoleVariablesEditorListRowPtr& Child : InRow->GetChildRows())
		{
			TreeViewPtr->SetItemExpansion(Child, bNewIsExpanded);
			Child->SetIsTreeViewItemExpanded(bNewIsExpanded);

			SetChildExpansionRecursively(Child, bNewIsExpanded);
		}
	}
};

#undef LOCTEXT_NAMESPACE

