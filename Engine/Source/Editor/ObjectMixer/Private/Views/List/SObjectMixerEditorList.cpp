// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/List/SObjectMixerEditorList.h"

// Filters
#include "Views/List/ObjectMixerEditorListFilters/ObjectMixerEditorListFilter_Source.h"

#include "ObjectMixerEditorLog.h"
#include "ObjectMixerEditorModule.h"
#include "ObjectMixerEditorStyle.h"
#include "Views/List/SObjectMixerEditorListRow.h"

#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Algo/Find.h"
#include "Components/LightComponent.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ObjectFilter/ObjectMixerEditorObjectFilter.h"
#include "Styling/StyleColors.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/SRichTextBlock.h"

#define LOCTEXT_NAMESPACE "ObjectMixerEditorList"

const FName SObjectMixerEditorList::ItemNameColumnName(TEXT("Builtin_Name"));
const FName SObjectMixerEditorList::EditorVisibilityColumnName(TEXT("Builtin_EditorVisibility"));
const FName SObjectMixerEditorList::EditorVisibilitySoloColumnName(TEXT("Builtin_EditorVisibilitySolo"));

void SObjectMixerEditorList::Construct(const FArguments& InArgs, TSharedRef<FObjectMixerEditorList> ListModel)
{
	ListModelPtr = ListModel;
	
	// Set Default Sorting info
	ActiveSortingType = EColumnSortMode::Ascending;
	
	HeaderRow = SNew(SHeaderRow)
				.CanSelectGeneratedColumn(false)
				.Visibility(EVisibility::Visible)
				;

	SetupFilters();
	
	ChildSlot
	[
		SNew(SVerticalBox)
		
		+ SVerticalBox::Slot()
		.VAlign(VAlign_Top)
		.AutoHeight()
		.Padding(FMargin(8.f, 0.f, 8.f, 0.f))
		[
			SNew(SHorizontalBox)

			+SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.Padding(0.f, 1.f, 0.f, 1.f)
			[
				SAssignNew(ListSearchBoxPtr, SSearchBox)
				.HintText(LOCTEXT("SearchHintText", "Search Scene Objects"))
				.ToolTipText(LOCTEXT("ObjectMixerEditorList_TooltipText", "Search Scene Objects"))
				.OnTextChanged_Raw(this, &SObjectMixerEditorList::OnListViewSearchTextChanged)
			]

			// Show Options
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(8.f, 1.f, 0.f, 1.f)
			[
				SAssignNew( ViewOptionsComboButton, SComboButton )
				.ContentPadding(4.f)
				.ToolTipText(LOCTEXT("ShowOptions_Tooltip", "Show options to affect the visibility of items in the Object Mixer list"))
				.ComboButtonStyle( FAppStyle::Get(), "SimpleComboButtonWithIcon" ) // Use the tool bar item style for this button
				.OnGetMenuContent( this, &SObjectMixerEditorList::BuildShowOptionsMenu)
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
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda([this]()
			{
				return DoesTreeViewHaveVisibleChildren() ? 0 : 1;
			})
			
			+ SWidgetSwitcher::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(2.0f, 2.0f, 2.0f, 2.0f)
			[
				SAssignNew(TreeViewPtr, STreeView<FObjectMixerEditorListRowPtr>)
				.HeaderRow(HeaderRow)
				.SelectionMode(ESelectionMode::Multi)
				.OnSelectionChanged_Lambda([this] (const FObjectMixerEditorListRowPtr& Row, const ESelectInfo::Type SelectionType)
				{
					if (GEditor && FSlateApplication::Get().GetModifierKeys().IsAltDown())
					{
						GEditor->SelectNone(false, true, true);

						for (const TSharedPtr<FObjectMixerEditorListRow>& SelectedRow : TreeViewPtr->GetSelectedItems())
						{
							if (SelectedRow->GetRowType() == FObjectMixerEditorListRow::SingleItem)
							{
								AActor* Actor = Cast<AActor>(SelectedRow->GetObject());

								if (!Actor)
								{
									Actor = SelectedRow->GetObject()->GetTypedOuter<AActor>();
								}

								if (Actor)
								{
									GEditor->SelectActor( Actor, true, true, true );
								}
							}
						}
					}
				})
				.TreeItemsSource(&VisibleTreeViewObjects)
				.OnGenerateRow_Lambda([this](FObjectMixerEditorListRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable)
					{
						check(Row.IsValid());
					
						return SNew(SObjectMixerEditorListRow, TreeViewPtr.ToSharedRef(), Row)
								.Visibility_Raw(Row.Get(), &FObjectMixerEditorListRow::GetDesiredVisibility);
					})
				.OnGetChildren_Raw(this, &SObjectMixerEditorList::OnGetRowChildren)
				.OnExpansionChanged_Raw(this, &SObjectMixerEditorList::OnRowChildExpansionChange, false)
				.OnSetExpansionRecursive(this, &SObjectMixerEditorList::OnRowChildExpansionChange, true)
			]

			// For when no rows exist in view
			+ SWidgetSwitcher::Slot()
			.HAlign(HAlign_Fill)
			.Padding(2.0f, 24.0f, 2.0f, 2.0f)
			[
				SNew(SRichTextBlock)
				.DecoratorStyleSet(&FAppStyle::Get())
				.AutoWrapText(true)
				.Justification(ETextJustify::Center)
				.Text_Lambda([this]()
				{
					// Preset Empty List (with filter)
                    return LOCTEXT("EmptyListPresetWithFilter", "No matching items in your list.\n\nCheck your filters.");
				})
			]
		]
	];
}

SObjectMixerEditorList::~SObjectMixerEditorList()
{	
	HeaderRow.Reset();
	
	ListSearchBoxPtr.Reset();
	ViewOptionsComboButton.Reset();
	ListBoxContainerPtr.Reset();

	FlushMemory(false);

	ShowFilters.Reset();
	TreeViewPtr.Reset();
}

void SObjectMixerEditorList::RebuildList(const FString& InItemToScrollTo)
{
	GenerateTreeView();

	if (!InItemToScrollTo.IsEmpty())
	{
		FObjectMixerEditorListRowPtr ScrollToItem = nullptr;

		for (const FObjectMixerEditorListRowPtr& TreeItem : TreeViewRootObjects)
		{
			if (false)
			{
				ScrollToItem = TreeItem;
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

void SObjectMixerEditorList::RefreshList()
{
	if (TreeViewRootObjects.Num() > 0)
	{
		// Apply last search
		ExecuteListViewSearchOnAllRows(GetSearchStringFromSearchInputField(), false);

		// Enforce Sort
		const FName& SortingName = GetActiveSortingColumnName();
		ExecuteSort(SortingName, GetSortModeForColumn(SortingName), false);

		// Show/Hide rows based on SetBy changes and filter settings
		EvaluateIfRowsPassFilters(false);
	}

	FindVisibleObjectsAndRequestTreeRefresh();
}

TArray<FObjectMixerEditorListRowPtr> SObjectMixerEditorList::GetSelectedTreeViewItems() const
{
	return TreeViewPtr->GetSelectedItems();
}

int32 SObjectMixerEditorList::GetSelectedTreeViewItemCount() const
{
	return TreeViewPtr->GetSelectedItems().Num();
}

void SObjectMixerEditorList::SetSelectedTreeViewItemActorsEditorVisible(const bool bNewIsVisible)
{
	for (const TSharedPtr<FObjectMixerEditorListRow>& SelectedItem : TreeViewPtr->GetSelectedItems())
	{
		SelectedItem->SetObjectVisibility(bNewIsVisible);
	}
}

bool SObjectMixerEditorList::IsTreeViewItemSelected(TSharedRef<FObjectMixerEditorListRow> Item)
{
	return TreeViewPtr->GetSelectedItems().Contains(Item);
}

TArray<FObjectMixerEditorListRowPtr> SObjectMixerEditorList::GetTreeViewItems() const
{
	return TreeViewRootObjects;
}

void SObjectMixerEditorList::SetTreeViewItems(const TArray<FObjectMixerEditorListRowPtr>& InItems)
{
	TreeViewRootObjects = InItems;

	TreeViewPtr->RequestListRefresh();
}

FString SObjectMixerEditorList::GetSearchStringFromSearchInputField() const
{
	return ensureAlwaysMsgf(ListSearchBoxPtr.IsValid(),
		TEXT("%hs: ListSearchBoxPtr is not valid. Check to make sure it was created."), __FUNCTION__)
	? ListSearchBoxPtr->GetText().ToString() : "";
}

void SObjectMixerEditorList::SetSearchStringInSearchInputField(const FString InSearchString) const
{
	if (ensureAlwaysMsgf(ListSearchBoxPtr.IsValid(),
		TEXT("%hs: ListSearchBoxPtr is not valid. Check to make sure it was created."), __FUNCTION__))
	{
		ListSearchBoxPtr->SetText(FText::FromString(InSearchString));
	}
}

void SObjectMixerEditorList::ExecuteListViewSearchOnAllRows(
	const FString& SearchString, const bool bShouldRefreshAfterward)
{
	TArray<FString> Tokens;
	
	// unquoted search equivalent to a match-any-of search
	SearchString.ParseIntoArray(Tokens, TEXT("|"), true);
	
	for (const TSharedPtr<FObjectMixerEditorListRow>& ChildRow : TreeViewRootObjects)
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

	if (bShouldRefreshAfterward)
	{
		FindVisibleObjectsAndRequestTreeRefresh();
	}
}

bool SObjectMixerEditorList::DoesTreeViewHaveVisibleChildren() const
{
	if (TreeViewPtr.IsValid())
	{
		for (const TSharedPtr<FObjectMixerEditorListRow>& Header : TreeViewRootObjects)
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

void SObjectMixerEditorList::SetTreeViewItemExpanded(const TSharedPtr<FObjectMixerEditorListRow>& RowToExpand, const bool bNewExpansion) const
{
	if (TreeViewPtr.IsValid())
	{
		TreeViewPtr->SetItemExpansion(RowToExpand, bNewExpansion);
	}
}

void SObjectMixerEditorList::ToggleFilterActive(const FString& FilterName)
{
	if (const TSharedRef<IObjectMixerEditorListFilter>* Match =
		Algo::FindByPredicate(ShowFilters,
		[&FilterName](TSharedRef<IObjectMixerEditorListFilter> Comparator)
		{
			return Comparator->GetFilterName().Equals(FilterName);
		}))
	{
		const TSharedRef<IObjectMixerEditorListFilter> Filter = *Match;
		Filter->ToggleFilterActive();

		EvaluateIfRowsPassFilters();
	}
}

void SObjectMixerEditorList::EvaluateIfRowsPassFilters(const bool bShouldRefreshAfterward)
{
	// Separate filters by type
	
	TSet<TSharedRef<IObjectMixerEditorListFilter>> MatchAnyOfFilters;
	TSet<TSharedRef<IObjectMixerEditorListFilter>> MatchAllOfFilters;

	for (const TSharedRef<IObjectMixerEditorListFilter>& Filter : ShowFilters)
	{
		if (Filter->GetFilterMatchType() ==
			IObjectMixerEditorListFilter::EObjectMixerEditorListFilterMatchType::MatchAll)
		{
			MatchAllOfFilters.Add(Filter);
		}
		else
		{
			MatchAnyOfFilters.Add(Filter);
		}
	}
	
	for (const FObjectMixerEditorListRowPtr& Row : TreeViewRootObjects)
	{
		if (Row.IsValid() && Row->GetRowType() == FObjectMixerEditorListRow::SingleItem)
		{
			auto Projection = [&Row](const TSharedRef<IObjectMixerEditorListFilter>& Filter)
			{
				return Filter->GetIsFilterActive() ? Filter->DoesItemPassFilter(Row) : true;
			};
			
			const bool bPassesAnyOf = MatchAnyOfFilters.Num() ? Algo::AnyOf(MatchAnyOfFilters, Projection) : true;
			const bool bPassesAllOf = MatchAllOfFilters.Num() ? Algo::AllOf(MatchAllOfFilters, Projection) : true;
			
			Row->SetDoesRowPassFilters(bPassesAnyOf && bPassesAllOf);
		}
	}

	if (bShouldRefreshAfterward)
	{
		FindVisibleObjectsAndRequestTreeRefresh();
	}
}

EColumnSortMode::Type SObjectMixerEditorList::GetSortModeForColumn(FName InColumnName) const
{
	EColumnSortMode::Type ColumnSortMode = EColumnSortMode::None;

	if (GetActiveSortingColumnName().IsEqual(InColumnName))
	{
		ColumnSortMode = ActiveSortingType;
	}

	return ColumnSortMode;
}

void SObjectMixerEditorList::OnSortColumnCalled(EColumnSortPriority::Type Priority, const FName& ColumnName, EColumnSortMode::Type SortMode)
{
	ExecuteSort(ColumnName, CycleSortMode(ColumnName));
}

EColumnSortMode::Type SObjectMixerEditorList::CycleSortMode(const FName& InColumnName)
{
	const EColumnSortMode::Type PreviousColumnSortMode = GetSortModeForColumn(InColumnName);
	ActiveSortingType = PreviousColumnSortMode ==
		EColumnSortMode::Ascending ? EColumnSortMode::Descending : EColumnSortMode::Ascending;

	ActiveSortingColumnName = InColumnName;
	return ActiveSortingType;
}

void SObjectMixerEditorList::ExecuteSort(
	const FName& InColumnName, const EColumnSortMode::Type InColumnSortMode, const bool bShouldRefreshAfterward)
{	
	if (bShouldRefreshAfterward)
	{
		FindVisibleObjectsAndRequestTreeRefresh();
	}
}

FListViewColumnInfo* SObjectMixerEditorList::GetColumnInfoByPropertyName(const FName& InPropertyName)
{
	return Algo::FindByPredicate(ListViewColumns,
		[InPropertyName] (const FListViewColumnInfo& ColumnInfo)
		{
			return ColumnInfo.PropertyName.IsEqual(InPropertyName);
		});
}

TSharedRef<SWidget> SObjectMixerEditorList::GenerateHeaderRowContextMenu() const
{
	FMenuBuilder MenuBuilder(false, nullptr);
	
	MenuBuilder.AddSearchWidget();

	FName LastCategoryName = NAME_None;

	for (const FListViewColumnInfo& ColumnInfo : ListViewColumns)
	{
		const FName& CategoryName = ColumnInfo.CategoryName;

		if (!CategoryName.IsEqual(LastCategoryName))
		{
			LastCategoryName = CategoryName;
			
			MenuBuilder.EndSection();
            MenuBuilder.BeginSection(LastCategoryName, FText::FromName(LastCategoryName));
		}
		
		const FName& PropertyName = ColumnInfo.PropertyName;
		
		const FText Tooltip = ColumnInfo.PropertyRef ?
			ColumnInfo.PropertyRef->GetToolTipText() : ColumnInfo.PropertyDisplayText;

		const bool bCanSelectColumn = ColumnInfo.PropertyType != EListViewColumnType::BuiltIn;

		const FName Hook = ColumnInfo.PropertyType == EListViewColumnType::BuiltIn ? "Builtin" : "GeneratedProperties";
		
		MenuBuilder.AddMenuEntry(
			ColumnInfo.PropertyDisplayText,
			Tooltip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, PropertyName]()
				{
					check(HeaderRow);
					
					HeaderRow->SetShowGeneratedColumn(PropertyName, !HeaderRow->IsColumnVisible(PropertyName));
				}),
				FCanExecuteAction::CreateLambda([bCanSelectColumn](){return bCanSelectColumn;}),
				FIsActionChecked::CreateLambda([this, PropertyName]()
				{
					check(HeaderRow);
					
					return HeaderRow->IsColumnVisible(PropertyName);
				})
			),
			Hook,
			EUserInterfaceActionType::Check
		);
	}

	return  MenuBuilder.MakeWidget();
}

bool SObjectMixerEditorList::AddUniquePropertyColumnsToHeaderRow(
	FProperty* Property,
	const bool bForceIncludeProperty,
	const TArray<FName>& PropertySkipList)
{
	if (!ensureAlwaysMsgf(Property, TEXT("%hs: Invalid property passed in. Please ensure only valid properties are passed to this function."), __FUNCTION__))
	{
		return false;
	}

	bool bShouldIncludeProperty = bForceIncludeProperty;

	if (!bShouldIncludeProperty)
	{
		const bool bIsPropertyBlueprintEditable = (Property->GetPropertyFlags() & CPF_Edit) != 0;

		// We don't have a proper way to display these yet
		const bool bDoesPropertyHaveSupportedClass =
			!Property->IsA(FMapProperty::StaticClass()) &&
			!Property->IsA(FArrayProperty::StaticClass()) &&
			!Property->IsA(FSetProperty::StaticClass()) &&
			!Property->IsA(FStructProperty::StaticClass());

		bShouldIncludeProperty = bIsPropertyBlueprintEditable && bDoesPropertyHaveSupportedClass;
	}

	if (bShouldIncludeProperty)
	{
		const bool bIsPropertyExplicitlySkipped =
		   PropertySkipList.Num() && PropertySkipList.Contains(Property->GetFName());

		bShouldIncludeProperty = !bIsPropertyExplicitlySkipped;
	}
	
	if (bShouldIncludeProperty)
	{
		const FName PropertyName = Property->GetFName();
	
		// Ensure no duplicate properties
		if (!Algo::FindByPredicate(ListViewColumns,
				[&PropertyName] (const FListViewColumnInfo& ListViewColumn)
				{
					return ListViewColumn.PropertyName.IsEqual(PropertyName);
				})
			)
		{
			ListViewColumns.Add(
				{
					Property, PropertyName,
					Property->GetDisplayNameText(),
					EListViewColumnType::PropertyGenerated,
					"Generated Properties",
					true, true, false
				}
			);

			return true;
		}
	}

	return false;
}

void SObjectMixerEditorList::AddBuiltinColumnsToHeaderRow()
{
	ListViewColumns.Insert(
		{
			nullptr, ItemNameColumnName,
			LOCTEXT("ItemNameHeaderText", "Name"),
			EListViewColumnType::BuiltIn, "Built-In",
			true, true,
			false, 1.0f, 1.7f
		}, 0
	);
	
	ListViewColumns.Insert(
		{
			nullptr, EditorVisibilitySoloColumnName,
			LOCTEXT("EditorVisibilitySoloColumnNameHeaderText", "Solo"),
			EListViewColumnType::BuiltIn, "Built-In",
			true, false,
			true, 25.0f
		}, 0
	);

	ListViewColumns.Insert(
		{
			nullptr, EditorVisibilityColumnName,
			LOCTEXT("EditorVisibilityColumnNameHeaderText", "Visibility"),
			EListViewColumnType::BuiltIn, "Built-In",
			true, false,
			true, 25.0f
		}, 0
	);
}

TSharedPtr<SHeaderRow> SObjectMixerEditorList::GenerateHeaderRow()
{
	check(ListModelPtr.IsValid());
	check(HeaderRow);

	TMap<FName, bool> LastVisibleColumns;
	for (const SHeaderRow::FColumn& Column : HeaderRow->GetColumns())
	{
		LastVisibleColumns.Add(Column.ColumnId, Column.bIsVisible);
	}
	
	HeaderRow->ClearColumns();
	ListViewColumns.Empty(ListViewColumns.Num());

	// Property Columns
	const TObjectPtr<UObjectMixerObjectFilter> SelectedFilter = ListModelPtr.Pin()->GetObjectFilter();
	if (!SelectedFilter)
	{
		UE_LOG(LogObjectMixerEditor, Display, TEXT("%hs: No classes defined in UObjectMixerObjectFilter class."), __FUNCTION__);
		return nullptr;
	}

	const EObjectMixerPropertyInheritanceInclusionOptions Options =
		SelectedFilter->GetObjectMixerPropertyInheritanceInclusionOptions();
	TArray<UClass*> SpecifiedClasses =
		SelectedFilter->GetParentAndChildClassesFromSpecifiedClasses(SelectedFilter->GetObjectClassesToFilter(), Options);
	
	const TArray<FName> PropertySkipList = SelectedFilter->GetColumnsFilter();
	const TArray<FName> ForceAddedPropertyList = SelectedFilter->GetForceAddedColumns();

	const bool bShouldIncludeUnsupportedProperties = SelectedFilter->ShouldIncludeUnsupportedProperties();
	
	for (const UClass* Class : SpecifiedClasses)
	{
		for (TFieldIterator<FProperty> FieldIterator(Class, EFieldIterationFlags::None); FieldIterator; ++FieldIterator)
		{
			if (FProperty* Property = *FieldIterator)
			{
				AddUniquePropertyColumnsToHeaderRow(Property, bShouldIncludeUnsupportedProperties, PropertySkipList);
			}
		}

		// Check Force Added Columns
		for (const FName& PropertyName : ForceAddedPropertyList)
		{
			if (FProperty* Property = FindFProperty<FProperty>(Class, PropertyName))
			{
				AddUniquePropertyColumnsToHeaderRow(Property, true);
			}
		}
	}

	// Alphabetical sort by Property Name
	ListViewColumns.StableSort([](const FListViewColumnInfo& A, const FListViewColumnInfo& B)
	{
		return A.PropertyDisplayText.ToString() < B.PropertyDisplayText.ToString();
	});

	// Alphabetical sort by Category Name
	ListViewColumns.StableSort([](const FListViewColumnInfo& A, const FListViewColumnInfo& B)
	{
		return A.CategoryName.LexicalLess(B.CategoryName);
	});

	// Add Built-in Columns to beginning
	AddBuiltinColumnsToHeaderRow();

	// Actually add columns to Header
	const FText ClickToSortTooltip = LOCTEXT("ClickToSort","Click to sort");
	
	const TArray<FName> ColumnsToShowByDefault = SelectedFilter->GetColumnsToShowByDefault();

	const TSharedRef<SWidget> HeaderMenuContent = GenerateHeaderRowContextMenu();

	for (const FListViewColumnInfo& ColumnInfo : ListViewColumns)
	{
		const FText Tooltip = ColumnInfo.PropertyRef ? ColumnInfo.PropertyRef->GetToolTipText() :
			ColumnInfo.bCanBeSorted ? ClickToSortTooltip : ColumnInfo.PropertyDisplayText;
		
		SHeaderRow::FColumn::FArguments Column =
			SHeaderRow::Column(ColumnInfo.PropertyName)
			.DefaultLabel(ColumnInfo.PropertyDisplayText)
			.ToolTipText(Tooltip)
			.HAlignHeader(EHorizontalAlignment::HAlign_Left)
		;

		if (ColumnInfo.bUseFixedWidth)
		{
			Column.FixedWidth(ColumnInfo.FixedWidth);
		}
		else
		{
			Column.FillWidth(ColumnInfo.FillWidth);
		}

		if (ColumnInfo.bCanBeSorted)
		{
			Column.SortMode_Raw(this, &SObjectMixerEditorList::GetSortModeForColumn, ColumnInfo.PropertyName);
			Column.OnSort_Raw(this, &SObjectMixerEditorList::OnSortColumnCalled);
		}

		if (ColumnInfo.PropertyType == EListViewColumnType::BuiltIn)
		{
			Column.ShouldGenerateWidget(true);
		}

		if (ColumnInfo.PropertyName.IsEqual(EditorVisibilityColumnName))
		{
			Column.HeaderContent()
			[
				SNew(SBox)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(0.f)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FAppStyle::Get().GetBrush("Level.VisibleIcon16x"))
				]
			];
		}
		else if (ColumnInfo.PropertyName.IsEqual(EditorVisibilitySoloColumnName))
		{
			Column.HeaderContent()
			[
				SNew(SBox)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(0.f)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FAppStyle::Get().GetBrush("MediaAsset.AssetActions.Solo.Small"))
				]
			];
		}
		else // Add Column Selection Menu widget to all other columns
		{
			Column.MenuContent()
			[
				HeaderMenuContent
			];
		}
		
		HeaderRow->AddColumn(Column);
		bool bShouldShowColumn = ColumnsToShowByDefault.Contains(ColumnInfo.PropertyName);

		if (const bool* Match = LastVisibleColumns.Find(ColumnInfo.PropertyName))
		{
			bShouldShowColumn = *Match;
		}
		
		HeaderRow->SetShowGeneratedColumn(ColumnInfo.PropertyName, bShouldShowColumn);
	}

	return HeaderRow;
}

void SObjectMixerEditorList::SetupFilters()
{
	
}

TSharedRef<SWidget> SObjectMixerEditorList::BuildShowOptionsMenu()
{
	FMenuBuilder ShowOptionsMenuBuilder = FMenuBuilder(true, nullptr);

	ShowOptionsMenuBuilder.AddMenuEntry(
		FText::FromString("Open Generic Object Mixer Instance"),
		FText::FromString("Open Generic Object Mixer Instance"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FObjectMixerEditorModule::ObjectMixerToolkitPanelTabId);
		})));

	ShowOptionsMenuBuilder.AddMenuEntry(FText::FromString("Refresh List"), FText::FromString("Refresh"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &SObjectMixerEditorList::GenerateTreeView)));

	ShowOptionsMenuBuilder.BeginSection("", LOCTEXT("ShowOptions_ShowSectionHeading", "Show"));
	{
		// Add show filters
		auto AddFiltersLambda = [this, &ShowOptionsMenuBuilder](const TSharedRef<IObjectMixerEditorListFilter>& InFilter)
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
				   FIsActionChecked::CreateSP( InFilter, &IObjectMixerEditorListFilter::GetIsFilterActive )
			   ),
			   NAME_None,
			   EUserInterfaceActionType::ToggleButton
		   );
		};

		for (const TSharedRef<IObjectMixerEditorListFilter>& Filter : ShowFilters)
		{
			AddFiltersLambda(Filter);
		}
	}
	ShowOptionsMenuBuilder.EndSection();

	return ShowOptionsMenuBuilder.MakeWidget();
}

void SObjectMixerEditorList::FlushMemory(const bool bShouldKeepMemoryAllocated)
{
	if (bShouldKeepMemoryAllocated)
	{
		TreeViewRootObjects.Reset();
		VisibleTreeViewObjects.Reset();
	}
	else
	{
		TreeViewRootObjects.Empty();
		VisibleTreeViewObjects.Empty();
	}
}

void SObjectMixerEditorList::SetAllGroupsCollapsed()
{
	if (TreeViewPtr.IsValid())
	{
		for (const FObjectMixerEditorListRowPtr& RootRow : TreeViewRootObjects)
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

void SObjectMixerEditorList::OnListViewSearchTextChanged(const FText& Text)
{
	ExecuteListViewSearchOnAllRows(Text.ToString(), true);
}

void SObjectMixerEditorList::GenerateTreeView()
{
	check(ListModelPtr.IsValid());
	
	if (!ensure(TreeViewPtr.IsValid()))
	{
		return;
	}
	
	FlushMemory(true);
	
	GenerateHeaderRow();

	TArray<UClass*> AcceptableClasses = ListModelPtr.Pin()->GetObjectClasses();

	check(GEditor);
	const UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	
	for (TObjectIterator<UObject> ObjectIterator; ObjectIterator; ++ObjectIterator)
	{
		if (UObject* Object = *ObjectIterator; Object->GetWorld() == EditorWorld)
		{
			bool bIsAcceptableClass = false;

			for (UClass* Class : AcceptableClasses)
			{
				if (Object->IsA(Class))
				{
					bIsAcceptableClass = true;
					break;
				}
			}

			if (bIsAcceptableClass)
			{
				TreeViewRootObjects.Add(
					MakeShared<FObjectMixerEditorListRow>(
						Object, FObjectMixerEditorListRow::SingleItem, SharedThis(this), nullptr)
				);
			}
		}
	}

	RefreshList();
}

void SObjectMixerEditorList::FindVisibleTreeViewObjects()
{
	VisibleTreeViewObjects.Empty();

	for (const TSharedPtr<FObjectMixerEditorListRow>& Row : TreeViewRootObjects)
	{
		if (Row->ShouldBeVisible())
		{
			VisibleTreeViewObjects.Add(Row);
		}
	}
}

void SObjectMixerEditorList::FindVisibleObjectsAndRequestTreeRefresh()
{
	FindVisibleTreeViewObjects();
	TreeViewPtr->RequestTreeRefresh();
}

void SObjectMixerEditorList::OnGetRowChildren(FObjectMixerEditorListRowPtr Row, TArray<FObjectMixerEditorListRowPtr>& OutChildren) const
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

void SObjectMixerEditorList::OnRowChildExpansionChange(FObjectMixerEditorListRowPtr Row, const bool bIsExpanded, const bool bIsRecursive) const
{
	if (Row.IsValid())
	{
		if (bIsRecursive)
		{
			if (bIsExpanded)
			{
				if (Row->GetRowType() == FObjectMixerEditorListRow::Group)
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

void SObjectMixerEditorList::SetChildExpansionRecursively(const FObjectMixerEditorListRowPtr& InRow, const bool bNewIsExpanded) const
{
	if (InRow.IsValid())
	{
		for (const FObjectMixerEditorListRowPtr& Child : InRow->GetChildRows())
		{
			TreeViewPtr->SetItemExpansion(Child, bNewIsExpanded);
			Child->SetIsTreeViewItemExpanded(bNewIsExpanded);

			SetChildExpansionRecursively(Child, bNewIsExpanded);
		}
	}
}

#undef LOCTEXT_NAMESPACE
