// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConsoleVariablesEditorList.h"

#include "ConsoleVariablesAsset.h"
#include "ConsoleVariablesEditorModule.h"
#include "SConsoleVariablesEditorListRow.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "ConsoleVariablesEditor"

void SConsoleVariablesEditorList::Construct(const FArguments& InArgs)
{
	DefaultNameText = LOCTEXT("ConsoleVariables", "Console Variables");

	HeaderDummyInfo = MakeShared<FConsoleVariablesEditorCommandInfo>("", nullptr, "");

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
				SNew(SOverlay)
				
				+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.Padding(2.0f, 2.0f, 2.0f, 2.0f)
				[
					SNew(SVerticalBox)

					// Slot for Header Row. Separate from other Tree View objects so that it doesn't scroll with the rest of the list
					+SVerticalBox::Slot()
					.AutoHeight()
					[
						SAssignNew(HeaderBoxPtr, SBox)
						.Padding(FMargin(10.f, 2.f, 0.f, 2.f))
					]

					+SVerticalBox::Slot()
					[
						SAssignNew(TreeViewPtr, STreeView<FConsoleVariablesEditorListRowPtr>)
						.SelectionMode(ESelectionMode::None)
						.TreeItemsSource(&TreeViewRootObjects)
						.OnGenerateRow_Lambda([this](FConsoleVariablesEditorListRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable)
							{
								check(Row.IsValid());
							
								return SNew(STableRow<FConsoleVariablesEditorListRowPtr>, OwnerTable)
									[
										SNew(SConsoleVariablesEditorListRow, Row, SplitterManagerPtr)
									]
									.Visibility_Raw(Row.Get(), &FConsoleVariablesEditorListRow::GetDesiredVisibility);
							})
						.OnGetChildren_Raw(this, &SConsoleVariablesEditorList::OnGetRowChildren)
						.OnExpansionChanged_Raw(this, &SConsoleVariablesEditorList::OnRowChildExpansionChange, false)
						.OnSetExpansionRecursive(this, &SConsoleVariablesEditorList::OnRowChildExpansionChange, true)
						.Visibility_Lambda([this]()
						{
							return this->DoesTreeViewHaveVisibleChildren() ? EVisibility::Visible : EVisibility::Collapsed;
						})
					]
				]

				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.Padding(2.0f, 24.0f, 2.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ConsoleVariablesEditorList_NoList", "No List to show. Try clearing the active search or adding some console variables to the list."))
					.Visibility_Lambda([this]()
						{
							return DoesTreeViewHaveVisibleChildren() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
						})
				]
			]
		];
}

SConsoleVariablesEditorList::~SConsoleVariablesEditorList()
{	
	ListSearchBoxPtr.Reset();
	ListBoxContainerPtr.Reset();

	SplitterManagerPtr.Reset();

	FlushMemory(false);

	HeaderBoxPtr.Reset();
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

	HeaderBoxPtr.Get()->SetContent(SNullWidget::NullWidget);
	EditedAsset.Reset();
}

void SConsoleVariablesEditorList::RefreshScroll() const
{
	TreeViewPtr->RequestListRefresh();
}

void SConsoleVariablesEditorList::RefreshList(TObjectPtr<UConsoleVariablesAsset> InAsset, const FString& InConsoleCommandToScrollTo)
{
	GenerateTreeView(InAsset);

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

void SConsoleVariablesEditorList::GenerateTreeView(UConsoleVariablesAsset* InAsset)
{	
	if (!ensure(TreeViewPtr.IsValid() && InAsset))
	{
		return;
	}
	
	FlushMemory(true);

	EditedAsset = InAsset;
	
	SplitterManagerPtr = MakeShared<FConsoleVariablesEditorListSplitterManager>(FConsoleVariablesEditorListSplitterManager());

	FConsoleVariablesEditorModule& ConsoleVariablesEditorModule = FConsoleVariablesEditorModule::Get();

	for (const TPair<FString, FString>& CommandAndValue : InAsset->GetSavedCommandsAndValues())
	{
		TWeakPtr<FConsoleVariablesEditorCommandInfo> CommandInfo = ConsoleVariablesEditorModule.FindCommandInfoByName(CommandAndValue.Key);
		
		if (CommandInfo.IsValid())
		{
			CommandInfo.Pin()->ExecuteCommand(CommandAndValue.Value);
			
			FConsoleVariablesEditorListRowPtr NewRow = 
				MakeShared<FConsoleVariablesEditorListRow>(
						CommandInfo.Pin(), CommandAndValue.Value, FConsoleVariablesEditorListRow::SingleCommand, 
						ECheckBoxState::Checked, SharedThis(this), nullptr);
			TreeViewRootObjects.Add(NewRow);
		}
	}

	if (TreeViewRootObjects.Num() > 0)
	{
		// Header
		HeaderRow = 
			MakeShared<FConsoleVariablesEditorListRow>(
					HeaderDummyInfo, "", FConsoleVariablesEditorListRow::HeaderRow, ECheckBoxState::Checked, SharedThis(this), nullptr
			);

		HeaderBoxPtr->SetContent(SNew(SConsoleVariablesEditorListRow, HeaderRow, SplitterManagerPtr));

		SortTreeViewObjects(SelectedSortType);

		TreeViewPtr->RequestListRefresh();

		// Apply last search
		ExecuteListViewSearchOnAllRows(GetSearchStringFromSearchInputField());
	}
}

void SConsoleVariablesEditorList::SortTreeViewObjects(EConsoleVariablesEditorSortType InSortType)
{
	auto SortByVariableName = [](const FConsoleVariablesEditorListRowPtr& A, const FConsoleVariablesEditorListRowPtr& B)
	{
		return A->GetCommandInfo().Pin()->Command < B->GetCommandInfo().Pin()->Command;
	};

	switch (InSortType)
	{
		case EConsoleVariablesEditorSortType::SortByVariableName:
			TreeViewRootObjects.StableSort(SortByVariableName);
			break;

		default:
			break;
	};
	
	
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

