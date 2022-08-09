// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/List/SObjectMixerEditorList.h"

// View Filters
#include "Views/List/ObjectMixerEditorListFilters/ObjectMixerEditorListFilter_Source.h"

#include "ObjectFilter/ObjectMixerEditorObjectFilter.h"
#include "ObjectMixerEditorLog.h"
#include "ObjectMixerEditorModule.h"
#include "ObjectMixerEditorProjectSettings.h"
#include "ObjectMixerEditorStyle.h"
#include "Views/List/SObjectMixerEditorListRow.h"
#include "Views/Widgets/SObjectMixerPlacementAssetMenuEntry.h"

#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SPositiveActionButton.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "PlacementMode/Public/IPlacementModeModule.h"
#include "Styling/StyleColors.h"
#include "UObject/UObjectIterator.h"
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
			GenerateToolbar()
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
						TArray<AActor*> ActorsToSelect;
						for (const TSharedPtr<FObjectMixerEditorListRow>& SelectedRow : TreeViewPtr->GetSelectedItems())
						{
							if (SelectedRow->GetRowType() == FObjectMixerEditorListRow::MatchingObject ||
								SelectedRow->GetRowType() == FObjectMixerEditorListRow::ContainerObject)
							{
								AActor* Actor = Cast<AActor>(SelectedRow->GetObject());

								if (!Actor)
								{
									Actor = SelectedRow->GetObject()->GetTypedOuter<AActor>();
								}

								if (Actor)
								{
									ActorsToSelect.Add(Actor);
								}
							}
						}

						if (ActorsToSelect.Num())
						{
							GEditor->SelectNone(true, true, true);

							for (AActor* Actor : ActorsToSelect)
							{
								GEditor->SelectActor( Actor, true, true, true );
							}
						}
					}
				})
				.TreeItemsSource(&VisibleTreeViewObjects)
				.OnGenerateRow_Lambda([this](FObjectMixerEditorListRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable)
					{
						check(Row.IsValid());
					
						return SNew(SObjectMixerEditorListRow, TreeViewPtr.ToSharedRef(), Row)
								.Visibility_Raw(Row.Get(), &FObjectMixerEditorListRow::GetDesiredRowWidgetVisibility);
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

TSharedRef<SWidget> SObjectMixerEditorList::GenerateToolbar()
{
	check(ListModelPtr.IsValid());
	
	TSharedRef<SHorizontalBox> ToolbarBox = SNew(SHorizontalBox);

	// Add object button
	ToolbarBox->AddSlot()
   .HAlign(HAlign_Left)
   .VAlign(VAlign_Center)
   .AutoWidth()
   .Padding(FMargin(0, 4))
   [
	   SNew(SPositiveActionButton)
	   .Text(LOCTEXT("AddObject", "Add"))
	   .OnGetMenuContent(FOnGetContent::CreateRaw(this, &SObjectMixerEditorList::OnGenerateAddObjectButtonMenu))
   ];

	ToolbarBox->AddSlot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Center)
	.Padding(0.f, 1.f, 0.f, 1.f)
	[
		SAssignNew(ListSearchBoxPtr, SSearchBox)
		.HintText(LOCTEXT("SearchHintText", "Search Scene Objects"))
		.ToolTipText(LOCTEXT("ObjectMixerEditorList_TooltipText", "Search Scene Objects"))
		.OnTextChanged_Raw(this, &SObjectMixerEditorList::OnListViewSearchTextChanged)
	];

	// Show Options
	ToolbarBox->AddSlot()
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
	];

	return ToolbarBox;
}

TSharedRef<SWidget> SObjectMixerEditorList::OnGenerateAddObjectButtonMenu() const
{
	check(ListModelPtr.IsValid());

	const TSet<TSubclassOf<AActor>> SubclassesOfActor = ListModelPtr.Pin()->GetObjectClassesToPlace();

	if (SubclassesOfActor.Num() > 0)
	{
		TSet<UClass*> ClassesToPlace =
		   ListModelPtr.Pin()->GetObjectFilter()->GetParentAndChildClassesFromSpecifiedClasses(
			   SubclassesOfActor,
			   ListModelPtr.Pin()->GetObjectFilter()->GetObjectMixerPlacementClassInclusionOptions());
	
		FMenuBuilder AddObjectButtonMenuBuilder = FMenuBuilder(true, nullptr);

		for (const UClass* Class : ClassesToPlace)
		{
			if (const UActorFactory* Factory = GEditor->FindActorFactoryForActorClass(Class))
			{
				AddObjectButtonMenuBuilder.AddWidget(
					SNew(SObjectMixerPlacementAssetMenuEntry, MakeShareable(new FPlaceableItem(*Factory->GetClass()))), FText::GetEmpty());
			}
		}

		return AddObjectButtonMenuBuilder.MakeWidget();
	}

	return
		SNew(SBox)
		.Padding(5)
		[
			SNew(STextBlock)
				.Text(LOCTEXT("NoPlaceableActorsDefinedWarning", "Please define some placeable actors in the\nfilter class by overriding GetObjectClassesToPlace."))
				.Font(FAppStyle::Get().GetFontStyle("NormalFontItalic"))
		]
	;
}

void SObjectMixerEditorList::RebuildList()
{
	bShouldRebuild = false;
	
	GenerateTreeView();
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

void SObjectMixerEditorList::RequestRebuildList(const FString& InItemToScrollTo)
{
	bShouldRebuild = true;
}

TArray<FObjectMixerEditorListRowPtr> SObjectMixerEditorList::GetSelectedTreeViewItems() const
{
	return TreeViewPtr->GetSelectedItems();
}

int32 SObjectMixerEditorList::GetSelectedTreeViewItemCount() const
{
	return TreeViewPtr->GetSelectedItems().Num();
}

void SObjectMixerEditorList::SetSelectedTreeViewItemActorsEditorVisible(const bool bNewIsVisible, const bool bIsRecursive)
{
	for (const TSharedPtr<FObjectMixerEditorListRow>& SelectedItem : TreeViewPtr->GetSelectedItems())
	{
		SelectedItem->SetObjectVisibility(bNewIsVisible, bIsRecursive);
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
			const EVisibility HeaderVisibility = Header->GetDesiredRowWidgetVisibility();
			
			if (HeaderVisibility != EVisibility::Hidden && HeaderVisibility != EVisibility::Collapsed)
			{
				return true;
			}
		}
	}
	
	return false;
}

bool SObjectMixerEditorList::IsTreeViewItemExpanded(const TSharedPtr<FObjectMixerEditorListRow>& Row) const
{
	if (TreeViewPtr.IsValid())
	{
		return TreeViewPtr->IsItemExpanded(Row);
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

EObjectMixerTreeViewMode SObjectMixerEditorList::GetTreeViewMode()
{
	const TSharedPtr<FObjectMixerEditorList> PinnedListModel = GetListModelPtr().Pin();
	check(PinnedListModel);
	
	return PinnedListModel->GetTreeViewMode();
}

void SObjectMixerEditorList::SetTreeViewMode(EObjectMixerTreeViewMode InViewMode)
{
	if (TSharedPtr<FObjectMixerEditorList> PinnedListModel = GetListModelPtr().Pin())
	{
		PinnedListModel->SetTreeViewMode(InViewMode);
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
		if (Row.IsValid() && Row->GetRowType() == FObjectMixerEditorListRow::MatchingObject)
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

void SObjectMixerEditorList::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime,
	const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (bShouldRebuild)
	{
		RebuildList();
	}
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
	const TSet<FName>& PropertySkipList)
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
	
	TSet<UClass*> SpecifiedClasses =
		ListModelPtr.Pin()->GetObjectFilter()->GetParentAndChildClassesFromSpecifiedClasses(
			ObjectClassesToFilterCache, PropertyInheritanceInclusionOptionsCache);
	
	for (const UClass* Class : SpecifiedClasses)
	{
		for (TFieldIterator<FProperty> FieldIterator(Class, EFieldIterationFlags::None); FieldIterator; ++FieldIterator)
		{
			if (FProperty* Property = *FieldIterator)
			{
				AddUniquePropertyColumnsToHeaderRow(Property, bShouldIncludeUnsupportedPropertiesCache, ColumnsToExcludeCache);
			}
		}

		// Check Force Added Columns
		for (const FName& PropertyName : ForceAddedColumnsCache)
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
	{	
		const FText ClickToSortTooltip = LOCTEXT("ClickToSort","Click to sort");

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
			bool bShouldShowColumn = ColumnsToShowByDefaultCache.Contains(ColumnInfo.PropertyName);

			if (const bool* Match = LastVisibleColumns.Find(ColumnInfo.PropertyName))
			{
				bShouldShowColumn = *Match;
			}
		
			HeaderRow->SetShowGeneratedColumn(ColumnInfo.PropertyName, bShouldShowColumn);
		}
	}

	return HeaderRow;
}

void SObjectMixerEditorList::SetupFilters()
{
	
}

TSharedRef<SWidget> SObjectMixerEditorList::OnGenerateFilterClassMenu()
{
	FMenuBuilder MenuBuilder(true, NULL);

	TArray<UClass*> DerivedClasses;
	GetDerivedClasses(UObjectMixerObjectFilter::StaticClass(), DerivedClasses, true);

	DerivedClasses.Remove(UObjectMixerObjectFilter::StaticClass());
	DerivedClasses.Remove(UObjectMixerBlueprintObjectFilter::StaticClass());

	DerivedClasses.Sort([](UClass& A, UClass& B)
	{
		return A.GetFName().LexicalLess(B.GetFName());
	});

	if (DerivedClasses.Num())
	{
		const TSharedPtr<FObjectMixerEditorList, ESPMode::ThreadSafe> PinnedList = ListModelPtr.Pin();
		check(PinnedList);
		
		MenuBuilder.BeginSection(NAME_None, LOCTEXT("SelectClassMenuSection", "Select Class"));
		{
			for (UClass* DerivedClass : DerivedClasses)
			{
				if (IsValid(DerivedClass))
				{
					if (DerivedClass->GetName().StartsWith(TEXT("SKEL_")) || DerivedClass->GetName().StartsWith(TEXT("REINST_")))
					{
						continue;
					}

					if (DerivedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_HideDropDown | CLASS_Deprecated))
					{
						continue;
					}
					
					MenuBuilder.AddMenuEntry(
					   FText::FromName(DerivedClass->GetFName()),
					   FText::GetEmpty(),
					   FSlateIcon(),
					   FUIAction(
						   FExecuteAction::CreateSP(PinnedList.ToSharedRef(), &FObjectMixerEditorList::SetObjectFilterClass, DerivedClass),
						   FCanExecuteAction::CreateLambda([](){ return true; }),
						   FIsActionChecked::CreateSP(PinnedList.ToSharedRef(), &FObjectMixerEditorList::IsClassSelected, DerivedClass)
					   ),
					   NAME_None,
					   EUserInterfaceActionType::RadioButton
				   );
				}
			}
		}
		MenuBuilder.EndSection();
	}
	else
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("NoFilterClassesAvailable", "No filter classes available."), FText::GetEmpty(), FSlateIcon(), FUIAction());
	}

	TSharedRef<SWidget> Widget = MenuBuilder.MakeWidget();
	FChildren* ChildWidgets = Widget->GetChildren();
	for (int32 ChildItr = 0; ChildItr < ChildWidgets->Num(); ChildItr++)
	{
		const TSharedRef<SWidget>& Child = ChildWidgets->GetChildAt(ChildItr);

		Child->EnableToolTipForceField(false);
	}
	Widget->EnableToolTipForceField(false);
	
	return Widget;
}

TSharedRef<SWidget> SObjectMixerEditorList::BuildShowOptionsMenu()
{
	FMenuBuilder ShowOptionsMenuBuilder = FMenuBuilder(true, nullptr);

	ShowOptionsMenuBuilder.BeginSection("ListViewOptions", LOCTEXT("FilterClassManagementSection", "Filter Class Management"));
	{
		// Filter Class Management Button
		const TSharedRef<SWidget> FilterClassManagementButton =
			SNew(SBox)
			.Padding(8, 0)
			[
				SNew(SComboButton)
				.ToolTipText(LOCTEXT("FilterClassManagementButton_Tooltip", "Select a filter class"))
				.ContentPadding(FMargin(4, 0.5f))
				.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton"))
				.OnGetMenuContent(this, &SObjectMixerEditorList::OnGenerateFilterClassMenu)
				.ForegroundColor(FStyleColors::Foreground)
				.MenuPlacement(EMenuPlacement::MenuPlacement_MenuRight)
				.ButtonContent()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.Padding(0, 1, 4, 0)
					.AutoWidth()
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("Icons.Filter"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]

					+ SHorizontalBox::Slot()
					.Padding(0, 1, 0, 0)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("FilterClassToolbarButton", "Object Filter Class"))
					]
				]
			];

		ShowOptionsMenuBuilder.AddWidget(FilterClassManagementButton, FText::GetEmpty());
	}
	ShowOptionsMenuBuilder.EndSection();

	// Add List View Mode Options
	ShowOptionsMenuBuilder.BeginSection("ListViewOptions", LOCTEXT("ListViewOptionsSection", "List View Options"));
	{
		// Foreach on uenum
		const FString EnumPath = "/Script/ObjectMixerEditor.EObjectMixerTreeViewMode";
		if (const UEnum* EnumPtr = FindObject<UEnum>(nullptr, *EnumPath, true))
		{
			for (int32 EnumItr = 0; EnumItr < EnumPtr->GetMaxEnumValue(); EnumItr++)
			{
				EObjectMixerTreeViewMode EnumValue = (EObjectMixerTreeViewMode)EnumItr;
				
				ShowOptionsMenuBuilder.AddMenuEntry(
					EnumPtr->GetDisplayNameTextByIndex(EnumItr),
					EnumPtr->GetToolTipTextByIndex(EnumItr),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([this, EnumValue]()
						{
							SetTreeViewMode(EnumValue);
						}),
						FCanExecuteAction::CreateLambda([](){ return true; }),
						FIsActionChecked::CreateLambda([this, EnumValue]()
						{
							return GetTreeViewMode() == EnumValue;
						})
					),
					NAME_None,
					EUserInterfaceActionType::RadioButton
				);
			}
		}
	}
	ShowOptionsMenuBuilder.EndSection();

	ShowOptionsMenuBuilder.BeginSection("MiscOptionsSection", LOCTEXT("MiscOptionsSection","Misc"));
	{
		ShowOptionsMenuBuilder.AddMenuEntry(
		   FText::FromString("Open Generic Object Mixer Instance"),
		   FText::FromString("Open Generic Object Mixer Instance"),
		   FSlateIcon(),
		   FUIAction(FExecuteAction::CreateLambda([]()
		   {
			   FGlobalTabmanager::Get()->TryInvokeTab(FObjectMixerEditorModule::Get().GetTabSpawnerId());
		   })));

		ShowOptionsMenuBuilder.AddMenuEntry(FText::FromString("Refresh List"), FText::FromString("Refresh"), FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &SObjectMixerEditorList::GenerateTreeView)));
	}
	ShowOptionsMenuBuilder.EndSection();

	if (ShowFilters.Num())
	{
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
	}

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

void SObjectMixerEditorList::CacheTreeState()
{
	struct Local
	{
		static void RecursivelyCacheTreeState(
			const TArray<FObjectMixerEditorListRowPtr>& InObjects,
			TMap<FString, bool>& TreeItemExpansionStateCache,
			TSharedPtr<STreeView<FObjectMixerEditorListRowPtr>> TreeViewPtr)
		{
			for (const TSharedPtr<FObjectMixerEditorListRow>& TreeViewItem : InObjects)
			{
				const FString ObjectName = TreeViewItem->GetDisplayName().ToString();

				if (!ObjectName.IsEmpty())
				{
					TreeItemExpansionStateCache.Add(ObjectName, TreeViewPtr->IsItemExpanded(TreeViewItem));
				}

				RecursivelyCacheTreeState(TreeViewItem->GetChildRows(), TreeItemExpansionStateCache, TreeViewPtr);
			}
		}
	};

	Local::RecursivelyCacheTreeState(TreeViewRootObjects, TreeItemExpansionStateCache, TreeViewPtr);
}

void SObjectMixerEditorList::RestoreTreeState(const bool bFlushCache)
{
	struct Local
	{
		static void RecursivelyRestoreTreeState(
			const TArray<FObjectMixerEditorListRowPtr>& InObjects,
			TMap<FString, bool>& TreeItemExpansionStateCache,
			TSharedPtr<STreeView<FObjectMixerEditorListRowPtr>> TreeViewPtr,
			const bool bExpandByDefault)
		{
			for (const TSharedPtr<FObjectMixerEditorListRow>& TreeViewItem : InObjects)
			{
				const FString ObjectName = TreeViewItem->GetDisplayName().ToString();

				if (!ObjectName.IsEmpty())
				{
					if (const bool* bExpansionStatePtr = TreeItemExpansionStateCache.Find(ObjectName))
					{
						TreeViewPtr->SetItemExpansion(TreeViewItem, *bExpansionStatePtr);
					}
					else
					{
						TreeViewPtr->SetItemExpansion(TreeViewItem, bExpandByDefault);
					}
				}

				RecursivelyRestoreTreeState(
					TreeViewItem->GetChildRows(), TreeItemExpansionStateCache, TreeViewPtr, bExpandByDefault);
			}
		}
	};
	
	bool bExpandByDefault = true;
	if (const UObjectMixerEditorProjectSettings* const Settings = GetDefault<UObjectMixerEditorProjectSettings>())
	{
		bExpandByDefault = Settings->bExpandTreeViewItemsByDefault;
	}
	
	Local::RecursivelyRestoreTreeState(
		TreeViewRootObjects, TreeItemExpansionStateCache, TreeViewPtr, bExpandByDefault);

	if (bFlushCache)
	{
		TreeItemExpansionStateCache.Empty();
	}
}

void SObjectMixerEditorList::BuildPerformanceCacheAndGenerateHeaderIfNeeded()
{
	// If any of the following overrides change, we need to regenerate the header row. Otherwise skip regeneration for performance reasons.
	// GetObjectClassesToFilter, GetColumnsToShowByDefault, GetColumnsToExclude,
	// GetForceAddedColumns, GetObjectMixerPropertyInheritanceInclusionOptions, ShouldIncludeUnsupportedProperties
	bool bNeedToGenerateHeaders = false;
	
	const TObjectPtr<UObjectMixerObjectFilter> SelectedFilter = ListModelPtr.Pin()->GetObjectFilter();
	if (!SelectedFilter)
	{
		UE_LOG(LogObjectMixerEditor, Display, TEXT("%hs: No classes defined in UObjectMixerObjectFilter class."), __FUNCTION__);
		return;
	}

	if (const TSet<UClass*> ObjectClassesToFilter = SelectedFilter->GetObjectClassesToFilter();
		ObjectClassesToFilter.Num() != ObjectClassesToFilterCache.Num() ||
		ObjectClassesToFilter.Difference(ObjectClassesToFilterCache).Num() > 0 || ObjectClassesToFilterCache.Difference(ObjectClassesToFilter).Num() > 0)
	{
		ObjectClassesToFilterCache = ObjectClassesToFilter;
		if (!bNeedToGenerateHeaders)
		{
			bNeedToGenerateHeaders = true;
		}
	}
	if (const TSet<FName> ColumnsToShowByDefault = SelectedFilter->GetColumnsToShowByDefault();
		ColumnsToShowByDefault.Num() != ColumnsToShowByDefaultCache.Num() ||
		ColumnsToShowByDefault.Difference(ColumnsToShowByDefaultCache).Num() > 0 || ColumnsToShowByDefaultCache.Difference(ColumnsToShowByDefault).Num() > 0)
	{
		ColumnsToShowByDefaultCache = ColumnsToShowByDefault;
		if (!bNeedToGenerateHeaders)
		{
			bNeedToGenerateHeaders = true;
		}
	}
	if (const TSet<FName> ColumnsToExclude = SelectedFilter->GetColumnsToExclude();
		ColumnsToExclude.Num() != ColumnsToExcludeCache.Num() ||
		ColumnsToExclude.Difference(ColumnsToExcludeCache).Num() > 0 || ColumnsToExcludeCache.Difference(ColumnsToExclude).Num() > 0)
	{
		ColumnsToExcludeCache = ColumnsToExclude;
		if (!bNeedToGenerateHeaders)
		{
			bNeedToGenerateHeaders = true;
		}
	}
	if (const TSet<FName> ForceAddedColumns = SelectedFilter->GetForceAddedColumns();
		ForceAddedColumns.Num() != ForceAddedColumnsCache.Num() ||
		ForceAddedColumns.Difference(ForceAddedColumnsCache).Num() > 0 || ForceAddedColumnsCache.Difference(ForceAddedColumns).Num() > 0)
	{
		ForceAddedColumnsCache = ForceAddedColumns;
		if (!bNeedToGenerateHeaders)
		{
			bNeedToGenerateHeaders = true;
		}
	}
	if (const EObjectMixerInheritanceInclusionOptions PropertyInheritanceInclusionOptions = SelectedFilter->GetObjectMixerPropertyInheritanceInclusionOptions(); 
		PropertyInheritanceInclusionOptions != PropertyInheritanceInclusionOptionsCache)
	{
		PropertyInheritanceInclusionOptionsCache = PropertyInheritanceInclusionOptions;
		if (!bNeedToGenerateHeaders)
		{
			bNeedToGenerateHeaders = true;
		}
	}
	if (const bool bShouldIncludeUnsupportedProperties = SelectedFilter->ShouldIncludeUnsupportedProperties(); 
		bShouldIncludeUnsupportedProperties != bShouldIncludeUnsupportedPropertiesCache)
	{
		bShouldIncludeUnsupportedPropertiesCache = bShouldIncludeUnsupportedProperties;
		if (!bNeedToGenerateHeaders)
		{
			bNeedToGenerateHeaders = true;
		}
	}
	
	if (bNeedToGenerateHeaders)
	{
		GenerateHeaderRow();
	}
}

void SObjectMixerEditorList::GenerateTreeView()
{
	check(ListModelPtr.IsValid());
	
	if (!ensure(TreeViewPtr.IsValid()))
	{
		return;
	}

	CacheTreeState();
	
	FlushMemory(true);

	BuildPerformanceCacheAndGenerateHeaderIfNeeded();

	check(GEditor);
	const UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();

	// Find valid matching objects
	TArray<UObject*> MatchingObjects;
	for (TObjectIterator<UObject> ObjectIterator; ObjectIterator; ++ObjectIterator)
	{
		if (UObject* Object = *ObjectIterator; IsValid(Object))
		{
			if (UWorld* ObjectWorld = Object->GetWorld(); ObjectWorld == EditorWorld)
			{
				bool bIsAcceptableClass = false;

				for (UClass* Class : ObjectClassesToFilterCache)
				{
					if (Object->IsA(Class))
					{
						bIsAcceptableClass = true;
						break;
					}
				}

				if (bIsAcceptableClass)
				{
					MatchingObjects.Add(Object);
				}
			}
		}
	}

	// A quick lookup for objects that already exist in the list.
	// Helpful to avoid double-generating rows when considering parent->child hierarchy.
	TMap<UObject*, TSharedRef<FObjectMixerEditorListRow>> CreatedObjectMap;
	TMap<FName, TSharedRef<FObjectMixerEditorListRow>> FolderMap;
	for (UObject* Object : MatchingObjects)
	{
		if (!CreatedObjectMap.Contains(Object)) // Ensure we don't double-generate container objects
		{
			TSharedRef<FObjectMixerEditorListRow> TopLevelRow = MakeShared<FObjectMixerEditorListRow>(
				Object, FObjectMixerEditorListRow::MatchingObject, SharedThis(this));

			CreatedObjectMap.Add(Object, TopLevelRow);

			// If the view is not in flat mode, we need to consider the hierarchy of outliner folders/attach parents as desired
			if (ListModelPtr.Pin()->GetTreeViewMode() != EObjectMixerTreeViewMode::Flat)
			{
				AActor* BaseActor = Cast<AActor>(Object);
				
				if (!BaseActor)
				{
					if (Object->IsA(UActorComponent::StaticClass()))
					{
						BaseActor = Object->GetTypedOuter<AActor>();

						// If it's not flat or folder view mode, we need to find or create the container object for the actor that owns the matching component
						if (ListModelPtr.Pin()->GetTreeViewMode() != EObjectMixerTreeViewMode::Folder)
						{
							TSharedPtr<FObjectMixerEditorListRow> OwningActorRow;

							if (const TSharedRef<FObjectMixerEditorListRow>* Match = CreatedObjectMap.Find(BaseActor))
							{
								OwningActorRow = *Match;
							}
							else
							{
								OwningActorRow = MakeShared<FObjectMixerEditorListRow>(
									BaseActor, FObjectMixerEditorListRow::ContainerObject, SharedThis(this));

								CreatedObjectMap.Add(BaseActor, OwningActorRow.ToSharedRef());
							}

							if (OwningActorRow)
							{
								OwningActorRow->AddToChildRows(TopLevelRow);
													
								TopLevelRow = OwningActorRow.ToSharedRef();
							}
						}
					}
				}

				if (BaseActor)
				{
					while (AActor* AttachParent = BaseActor->GetAttachParentActor())
					{
						// Make a row for each attach parent up the chain until we reach the top if not in flat/folder mode
						if (ListModelPtr.Pin()->GetTreeViewMode() != EObjectMixerTreeViewMode::Folder)
						{
							TSharedPtr<FObjectMixerEditorListRow> OwningActorRow;

							if (const TSharedRef<FObjectMixerEditorListRow>* Match = CreatedObjectMap.Find(AttachParent))
							{
								OwningActorRow = *Match;
							}
							else
							{
								OwningActorRow = MakeShared<FObjectMixerEditorListRow>(
									AttachParent, FObjectMixerEditorListRow::ContainerObject, SharedThis(this));

								CreatedObjectMap.Add(AttachParent, OwningActorRow.ToSharedRef());
							}

							if (OwningActorRow)
							{
								OwningActorRow->AddToChildRows(TopLevelRow);
												
								TopLevelRow = OwningActorRow.ToSharedRef();
							}
						}

						BaseActor = AttachParent;
					}

					// Now consider folder hierarchy for the base actor if desired
					if (ListModelPtr.Pin()->GetTreeViewMode() == EObjectMixerTreeViewMode::Folder ||
						ListModelPtr.Pin()->GetTreeViewMode() == EObjectMixerTreeViewMode::FolderObjectSubObject)
					{
						FFolder BaseActorFolder = BaseActor->GetFolder();

						while (!BaseActorFolder.IsNone())
						{
							TSharedPtr<FObjectMixerEditorListRow> FolderRow;
							
							if (const TSharedRef<FObjectMixerEditorListRow>* Match = FolderMap.Find(BaseActorFolder.GetPath()))
							{
								FolderRow = *Match;
							}
							else
							{
								FolderRow =
									MakeShared<FObjectMixerEditorListRow>(
										nullptr, FObjectMixerEditorListRow::Folder, SharedThis(this),
										FText::FromName(BaseActorFolder.GetLeafName()));

								FolderMap.Add(BaseActorFolder.GetPath(), FolderRow.ToSharedRef());
							}

							if (FolderRow)
							{
								FolderRow->AddToChildRows(TopLevelRow);
													
								TopLevelRow = FolderRow.ToSharedRef();
							}

							BaseActorFolder = BaseActorFolder.GetParent();
						}
					}
				}
			}

			TreeViewRootObjects.AddUnique(TopLevelRow);
		}
	}

	TreeViewRootObjects.StableSort(SortByTypeThenName);

	RefreshList();

	RestoreTreeState();
}

void SObjectMixerEditorList::FindVisibleTreeViewObjects()
{
	VisibleTreeViewObjects.Empty();

	for (const TSharedPtr<FObjectMixerEditorListRow>& Row : TreeViewRootObjects)
	{
		if (Row->ShouldRowWidgetBeVisible())
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
				if (Row->GetRowType() == FObjectMixerEditorListRow::Folder)
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
