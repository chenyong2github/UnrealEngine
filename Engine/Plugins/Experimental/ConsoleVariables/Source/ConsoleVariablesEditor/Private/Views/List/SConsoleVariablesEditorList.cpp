// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConsoleVariablesEditorList.h"

#include "ConsoleVariablesAsset.h"
#include "ConsoleVariablesEditorListFilters/ConsoleVariablesEditorListFilter_SourceText.h"
#include "ConsoleVariablesEditorModule.h"
#include "SConsoleVariablesEditorListRow.h"

#include "Algo/Find.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

#define LOCTEXT_NAMESPACE "ConsoleVariablesEditor"

const FName SConsoleVariablesEditorList::CustomSortOrderColumnName(TEXT("Order"));
const FName SConsoleVariablesEditorList::CheckBoxColumnName(TEXT("Column"));
const FName SConsoleVariablesEditorList::VariableNameColumnName(TEXT("Name"));
const FName SConsoleVariablesEditorList::ValueColumnName(TEXT("Value"));
const FName SConsoleVariablesEditorList::SourceColumnName(TEXT("Source"));

void SConsoleVariablesEditorList::Construct(const FArguments& InArgs)
{
	// Set Default Sorting info
	ActiveSortingColumnName = CustomSortOrderColumnName;
	ActiveSortingType = EColumnSortMode::Ascending;
	
	HeaderRow = SNew(SHeaderRow)
				.CanSelectGeneratedColumn(true).Visibility(EVisibility::Visible);
	
	GenerateHeaderRow();

	SetupFilters();
	
	ChildSlot
	[
		SNew(SVerticalBox)
		
		+ SVerticalBox::Slot()
		.VAlign(VAlign_Top)
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			+SHorizontalBox::Slot()
			.Padding(10.f, 1.f, 0.f, 1.f)
			[
				SAssignNew(ListSearchBoxPtr, SSearchBox)
				.HintText(LOCTEXT("ConsoleVariablesEditorList_SearchHintText", "Search tracked variables, values, sources or help text..."))
				.OnTextChanged_Raw(this, &SConsoleVariablesEditorList::OnListViewSearchTextChanged)
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(10.f, 1.f, 15.f, 1.f)
			.HAlign(HAlign_Right)
			[
				SAssignNew( ViewOptionsComboButton, SComboButton )
				.ComboButtonStyle( FAppStyle::Get(), "SimpleComboButtonWithIcon" ) // Use the tool bar item style for this button
				.OnGetMenuContent( this, &SConsoleVariablesEditorList::BuildShowOptionsMenu)
				.HasDownArrow(false)
				.ButtonContent()
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image( FAppStyle::Get().GetBrush("Icons.Settings") )
				]
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
				.SelectionMode(ESelectionMode::Multi)
				.OnSelectionChanged_Lambda([this] (const FConsoleVariablesEditorListRowPtr& Row, const ESelectInfo::Type SelectionType)
				{
					if(Row.IsValid())
					{
						Row->SetIsSelected(TreeViewPtr->GetSelectedItems().Contains(Row));
					}
				})
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
			.HAlign(HAlign_Fill)
			.Padding(2.0f, 24.0f, 2.0f, 2.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("ConsoleVariablesEditorList_NoList", "No List to show. Try clearing the active search or adding some console variables to the list."))
			]
		]
	];

	EvaluateIfRowsPassFilters();
}

SConsoleVariablesEditorList::~SConsoleVariablesEditorList()
{	
	ListSearchBoxPtr.Reset();
	ListBoxContainerPtr.Reset();
	ViewOptionsComboButton.Reset();

	FlushMemory(false);

	HeaderRow.Reset();

	TreeViewRootObjects.Empty();
	TreeViewPtr.Reset();
}

void SConsoleVariablesEditorList::SetupFilters()
{
	TArray<FString> SourceFilterTypes =
	{
		"Constructor",
		"Scalability",
		"Game Setting",
		"Project Setting",
		"System Settings ini",
		"Device Profile",
		"Game Override",
		"Console Variables ini",
		"Command line",
		"Code",
		"Console"
	};

	for (const FString& Type : SourceFilterTypes)
	{
		ShowFilters.Add(MakeShared<FConsoleVariablesEditorListFilter_SourceText>(Type));
	}
}

TSharedRef<SWidget> SConsoleVariablesEditorList::BuildShowOptionsMenu()
{
	FMenuBuilder ShowOptionsMenuBuilder = FMenuBuilder(true, nullptr);

	ShowOptionsMenuBuilder.BeginSection("AssetThumbnails", LOCTEXT("ShowOptionsShowSectionHeading", "Show"));
	{
		// Add mode filters
		auto AddFiltersLambda = [this, &ShowOptionsMenuBuilder](const TSharedRef<IConsoleVariablesEditorListFilter>& InFilter)
		{
			const FString& FilterName = InFilter->GetFilterName();
			
			ShowOptionsMenuBuilder.AddMenuEntry(
			   InFilter->GetFilterButtonLabel(),
			   InFilter->GetFilterButtonToolTip(),
			   FSlateIcon(),
			   FUIAction(
				   FExecuteAction::CreateLambda(
				   	[this, FilterName]()
					   {
						   ToggleFilterActive(FilterName);
					   }
					),
				   FCanExecuteAction(),
				   FIsActionChecked::CreateSP( InFilter, &IConsoleVariablesEditorListFilter::GetIsFilterActive )
			   ),
			   NAME_None,
			   EUserInterfaceActionType::ToggleButton
		   );
		};

		for (const TSharedRef<IConsoleVariablesEditorListFilter>& Filter : ShowFilters)
		{
			AddFiltersLambda(Filter);
		}
	}
	ShowOptionsMenuBuilder.EndSection();
	
	ShowOptionsMenuBuilder.BeginSection("AssetThumbnails", LOCTEXT("SortHeading", "Sort"));
	{
		// Add commands

		// Save this for later when folders are added
		/*ShowOptionsMenuBuilder.AddMenuEntry(
			LOCTEXT("CollapseAll", "Collapse All"),
			LOCTEXT("ConsoleVariablesEditorList_CollapseAll_Tooltip", "Collapse all expanded actor groups in the Modified Actors list."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &SConsoleVariablesEditorList::SetAllGroupsCollapsed)),
			NAME_None,
			EUserInterfaceActionType::Button
		);*/
		
		ShowOptionsMenuBuilder.AddMenuEntry(
			LOCTEXT("SetSortOrder", "Set Sort Order"),
			LOCTEXT("ConsoleVariablesEditorList_SetSortOrder_Tooltip", "Makes the current order of the variables list the saved order."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &SConsoleVariablesEditorList::SetSortOrder)),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	ShowOptionsMenuBuilder.EndSection();

	return ShowOptionsMenuBuilder.MakeWidget();
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

void SConsoleVariablesEditorList::RebuildList(const FString& InConsoleCommandToScrollTo)
{
	GenerateTreeView();

	RefreshList();

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

void SConsoleVariablesEditorList::RefreshList()
{
	if (TreeViewRootObjects.Num() > 0)
	{
		// Apply last search
		ExecuteListViewSearchOnAllRows(GetSearchStringFromSearchInputField());

		// Enforce Sort
		const FName& SortingName = GetActiveSortingColumnName();
		ExecuteSort(SortingName, GetSortModeForColumn(SortingName));

		// Show/Hide rows based on SetBy changes and filter settings
		EvaluateIfRowsPassFilters();

		TreeViewPtr->RequestTreeRefresh();
	}
}

TArray<FConsoleVariablesEditorListRowPtr> SConsoleVariablesEditorList::GetSelectedTreeViewItems() const
{
	return TreeViewPtr->GetSelectedItems();
}

TArray<FConsoleVariablesEditorListRowPtr> SConsoleVariablesEditorList::GetTreeViewItems() const
{
	return TreeViewRootObjects;
}

void SConsoleVariablesEditorList::SetTreeViewItems(const TArray<FConsoleVariablesEditorListRowPtr>& InItems)
{
	TreeViewRootObjects = InItems;

	TreeViewPtr->RequestListRefresh();
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
							ECheckBoxState::Checked, SharedThis(this), TreeViewRootObjects.Num(), nullptr);
				TreeViewRootObjects.Add(NewRow);
			}
		}
	}

	TreeViewPtr->RequestTreeRefresh();
}

TSharedPtr<SHeaderRow> SConsoleVariablesEditorList::GenerateHeaderRow()
{
	check(HeaderRow);
	HeaderRow->ClearColumns();

	HeaderRow->AddColumn(
		SHeaderRow::Column(CustomSortOrderColumnName)
			.DefaultLabel(FText::FromString("#"))
			.ToolTipText(LOCTEXT("ClickToSort","Click to sort"))
			.HAlignHeader(EHorizontalAlignment::HAlign_Center)
			.FillWidth(0.3f)
			.ShouldGenerateWidget(true)
			.SortMode_Raw(this, &SConsoleVariablesEditorList::GetSortModeForColumn, CustomSortOrderColumnName)
			.OnSort_Raw(this, &SConsoleVariablesEditorList::OnSortColumnCalled)
	);
	
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
			.ToolTipText(LOCTEXT("ClickToSort","Click to sort"))
			.HAlignHeader(EHorizontalAlignment::HAlign_Left)
			.FillWidth(1.7f)
			.ShouldGenerateWidget(true)
			.SortMode_Raw(this, &SConsoleVariablesEditorList::GetSortModeForColumn, VariableNameColumnName)
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
			.ToolTipText(LOCTEXT("ClickToSort","Click to sort"))
			.HAlignHeader(EHorizontalAlignment::HAlign_Left)
			.SortMode_Raw(this, &SConsoleVariablesEditorList::GetSortModeForColumn, SourceColumnName)
			.OnSort_Raw(this, &SConsoleVariablesEditorList::OnSortColumnCalled)
	);

	return HeaderRow;
}

void SConsoleVariablesEditorList::SetAllGroupsCollapsed()
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
}

void SConsoleVariablesEditorList::SetSortOrder()
{
	for (int32 RowItr = 0; RowItr < TreeViewRootObjects.Num(); RowItr++)
	{
		const TSharedPtr<FConsoleVariablesEditorListRow>& ChildRow = TreeViewRootObjects[RowItr];
		ChildRow->SetSortOrder(RowItr);
	}

	ExecuteSort(CustomSortOrderColumnName, CycleSortMode(CustomSortOrderColumnName));
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

void SConsoleVariablesEditorList::ToggleFilterActive(const FString& FilterName)
{
	if (const TSharedRef<IConsoleVariablesEditorListFilter>* Match =
		Algo::FindByPredicate(ShowFilters,
		[&FilterName](TSharedRef<IConsoleVariablesEditorListFilter> Comparator)
		{
			return Comparator->GetFilterName().Equals(FilterName);
		}))
	{
		const TSharedRef<IConsoleVariablesEditorListFilter> Filter = *Match;
		Filter->ToggleFilterActive();

		EvaluateIfRowsPassFilters();
	}
}

void SConsoleVariablesEditorList::EvaluateIfRowsPassFilters()
{
	for (const FConsoleVariablesEditorListRowPtr& Row : TreeViewRootObjects)
	{
		if (Row.IsValid() && Row->GetRowType() == FConsoleVariablesEditorListRow::SingleCommand)
		{
			Row->SetDoesRowPassFilters(Algo::FindByPredicate(
				ShowFilters,
				[&Row](const TSharedRef<IConsoleVariablesEditorListFilter>& Filter)
				{
					return Filter->GetIsFilterActive() && Filter->DoesItemPassFilter(Row);
				}) != nullptr);
		}
	}

	TreeViewPtr->RequestTreeRefresh();
}

EColumnSortMode::Type SConsoleVariablesEditorList::GetSortModeForColumn(FName InColumnName) const
{
	EColumnSortMode::Type ColumnSortMode = EColumnSortMode::None;

	if (GetActiveSortingColumnName().IsEqual(InColumnName))
	{
		ColumnSortMode = ActiveSortingType;
	}

	return ColumnSortMode;
}

void SConsoleVariablesEditorList::OnSortColumnCalled(EColumnSortPriority::Type Priority, const FName& ColumnName, EColumnSortMode::Type SortMode)
{
	ExecuteSort(ColumnName, CycleSortMode(ColumnName));
}

EColumnSortMode::Type SConsoleVariablesEditorList::CycleSortMode(const FName& InColumnName)
{
	// Custom handler for Custom Sort Order mode
	if (InColumnName.IsEqual(CustomSortOrderColumnName))
	{
		ActiveSortingType = EColumnSortMode::Ascending;
	}
	else
	{
		const EColumnSortMode::Type PreviousColumnSortMode = GetSortModeForColumn(InColumnName);
		ActiveSortingType = PreviousColumnSortMode ==
			EColumnSortMode::Ascending ? EColumnSortMode::Descending : EColumnSortMode::Ascending;
	}

	ActiveSortingColumnName = InColumnName;
	return ActiveSortingType;
}

void SConsoleVariablesEditorList::ExecuteSort(const FName& InColumnName, const EColumnSortMode::Type InColumnSortMode)
{	
	if (InColumnName.IsEqual(CustomSortOrderColumnName))
	{
		TreeViewRootObjects.StableSort(SortByOrderAscending);
	}
	if (InColumnName.IsEqual(SourceColumnName))
	{
		TreeViewRootObjects.StableSort(
			InColumnSortMode == EColumnSortMode::Ascending ? SortBySourceAscending : SortBySourceDescending);
	}
	if (InColumnName.IsEqual(VariableNameColumnName))
	{
		TreeViewRootObjects.StableSort(
			InColumnSortMode == EColumnSortMode::Ascending ? SortByVariableNameAscending : SortByVariableNameDescending);
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
