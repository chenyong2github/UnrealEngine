// Copyright Epic Games, Inc. All Rights Reserved.

#include "STraceDataFilterWidget.h"
#include "EditorStyleSet.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Templates/SharedPointer.h"
#include "SFilterPresetList.h"
#include "STraceObjectRowWidget.h"

#include "ITraceObject.h"
#include "TraceChannel.h"

#define LOCTEXT_NAMESPACE "STraceDataFilterWidget"

TSet<FString> STraceDataFilterWidget::LastExpandedObjectNames;

STraceDataFilterWidget::STraceDataFilterWidget() : FilterService(FEventFilterService::Get()), bNeedsTreeRefresh(false), bHighlightingPreset(false)
{
}

STraceDataFilterWidget::~STraceDataFilterWidget()
{
	// Save expansion and move to static array
	SaveItemsExpansion();
	LastExpandedObjectNames = ExpandedObjectNames;
	ExpandedObjectNames.Empty();
}

void STraceDataFilterWidget::Construct(const FArguments& InArgs)
{
	ConstructSearchBoxFilter();
	ConstructFilterHandler();
	ConstructTreeview();

	// Retrieve trace services module for session selection widget
	ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");

	ChildSlot
	[		
		SNew(SBorder)
		.Padding(4)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew( SVerticalBox )

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 2.0f))
			[
				// Filtering button and search box widgets
				SAssignNew(OptionsWidget, SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 2.0f, 0.0f)
				[
					SNew(SComboButton)
					.Visibility(EVisibility::Visible)
					.ComboButtonStyle(FEditorStyle::Get(), "GenericFilters.ComboButtonStyle")
					.ForegroundColor(FLinearColor::White)
					.ContentPadding(0.0f)
					.OnGetMenuContent(this, &STraceDataFilterWidget::MakeAddFilterMenu)
					.ButtonContent()
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
							.Text(FText::FromString(FString(TEXT("\xf0b0"))) /*fa-filter*/)
						]
						+SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2, 0, 0, 0)
						.VAlign(VAlign_Center)
						[
							SNew( STextBlock )
							.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
							.Text( LOCTEXT("PresetsMenuLabel", "Filter Presets") )
						]
					]
				]
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(SearchBoxWidget, SSearchBox)
					.SelectAllTextWhenFocused( true )
					.HintText( LOCTEXT( "SearchBoxHint", "Search Trace Events..."))
					.OnTextChanged(this, &STraceDataFilterWidget::OnSearchboxTextChanged)
				]
			]

			+ SVerticalBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, 2.0f)
			.AutoHeight()
			[
				// Presets bar widget
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(FilterPresetsListWidget, SFilterPresetList)
					.OnPresetsChanged(this, &STraceDataFilterWidget::OnPresetsChanged)
					.OnSavePreset(this, &STraceDataFilterWidget::OnSavePreset)
					.OnHighlightPreset(this, &STraceDataFilterWidget::OnHighlightPreset)
				]
			]

			+ SVerticalBox::Slot()	
			.Padding(0.0f, 0.0f, 0.0f, 2.0f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SNew(SScrollBorder, Treeview.ToSharedRef())
					[
						Treeview.ToSharedRef()					
					]
				]
			]			
		]	
	];

	/** Setup attribute to enable/disabled all widgets according to whether or not there is a valid session to represent */
	TAttribute<bool> EnabledAttribute;
	EnabledAttribute.Bind(this, &STraceDataFilterWidget::HasValidFilterSession);

	Treeview->SetEnabled(EnabledAttribute);
	OptionsWidget->SetEnabled(EnabledAttribute);
	FilterPresetsListWidget->SetEnabled(EnabledAttribute);

	/** Restore expansion state when recreating this window */
	ExpandedObjectNames = LastExpandedObjectNames;
	RestoreItemsExpansion();
}

void STraceDataFilterWidget::OnSavePreset(const TSharedPtr<IFilterPreset>& Preset)
{
	if (SessionFilterService.IsValid())
	{
		/** Save to preset if one was provided, otherwise create a new one */
		if (Preset.IsValid())
		{
			Preset->Save(RootItems);
		}
		else
		{
			FFilterPresetHelpers::CreateNewPreset(RootItems);
		}
	}
}

void STraceDataFilterWidget::OnPresetsChanged()
{
	TArray<TSharedPtr<IFilterPreset>> Presets;
	FilterPresetsListWidget->GetAllEnabledPresets(Presets);

	if (SessionFilterService.IsValid())
	{
		SessionFilterService->UpdateFilterPresets(Presets);
	}
}

void STraceDataFilterWidget::OnHighlightPreset(const TSharedPtr<IFilterPreset>& Preset)
{
	if (Treeview.IsValid())
	{
		/** Update treeview so that any whitelisted entry (as part of Preset) is highlighted and expanded */
		Treeview->ClearHighlightedItems();
		if (Preset.IsValid())
		{
			if (!bHighlightingPreset)
			{
				/** Store current expansion, so we can reset once highlighting has finished */
				SaveItemsExpansion();
				bHighlightingPreset = true;
			}

			TArray<FString> Names;
			Preset->GetWhitelistedNames(Names);

			EnumerateAllItems([this, Names](TSharedPtr<ITraceObject> Object) -> void
			{
				if (Names.Contains(Object->GetName()))
				{
					Treeview->SetItemHighlighted(Object, true);
					// TODO need to set parent expansion as well
					Treeview->SetItemExpansion(Object, true);

					SetParentExpansionRecursively(Object, true);
				}
			});
		}
		else
		{
			bHighlightingPreset = false;
			RestoreItemsExpansion();
		}
	}
}

void STraceDataFilterWidget::OnSearchboxTextChanged(const FText& FilterText)
{
	bNeedsTreeRefresh = true;
	const bool bFilterSet = !FilterText.IsEmpty();
	if (bFilterSet != TreeviewFilterHandler->GetIsEnabled())
	{
		TreeviewFilterHandler->SetIsEnabled(bFilterSet);

		if (bFilterSet)
		{
			SaveItemsExpansion();
		}
		else
		{
			RestoreItemsExpansion();
		}
	}

	SearchBoxWidgetFilter->SetRawFilterText(FilterText);
	SearchBoxWidget->SetError(SearchBoxWidgetFilter->GetFilterErrorText());
}

void STraceDataFilterWidget::ConstructTreeview()
{
	SAssignNew(Treeview, STreeView<TSharedPtr<ITraceObject>>)
	.ItemHeight(20.0f)
	.OnGetChildren(TreeviewFilterHandler.ToSharedRef(), &TreeFilterHandler<TSharedPtr<ITraceObject>>::OnGetFilteredChildren)
	.OnGenerateRow(this, &STraceDataFilterWidget::OnGenerateRow)
	.OnContextMenuOpening(this, &STraceDataFilterWidget::OnContextMenuOpening)
	.OnMouseButtonDoubleClick(this, &STraceDataFilterWidget::OnItemDoubleClicked)
	.OnSetExpansionRecursive_Lambda([this](TSharedPtr<ITraceObject> InObject, bool bInExpansionState)
	{
		SetExpansionRecursively(InObject, bInExpansionState);
	})
	.TreeItemsSource(&TreeItems);
	
	TreeviewFilterHandler->SetTreeView(Treeview.Get());
}

void STraceDataFilterWidget::OnItemDoubleClicked(TSharedPtr<ITraceObject> InObject) const
{
	TSharedPtr<ITableRow> Row = Treeview->WidgetFromItem(InObject);
	if (Row.IsValid())
	{
		SetExpansionRecursively(InObject, !Row->IsItemExpanded());
	}
}

void STraceDataFilterWidget::ConstructSearchBoxFilter()
{
	SearchBoxWidgetFilter = MakeShareable(new TTextFilter<TSharedPtr<ITraceObject>>(TTextFilter<TSharedPtr<ITraceObject>>::FItemToStringArray::CreateLambda([](TSharedPtr<ITraceObject> Object, TArray<FString>& OutStrings)
	{
		Object->GetSearchString(OutStrings);
	})));
}

void STraceDataFilterWidget::ConstructFilterHandler()
{
	TreeviewFilterHandler = MakeShareable(new TreeFilterHandler<TSharedPtr<ITraceObject>>());
	TreeviewFilterHandler->SetFilter(SearchBoxWidgetFilter.Get());
	TreeviewFilterHandler->SetRootItems(&RootItems, &TreeItems);
	TreeviewFilterHandler->SetGetChildrenDelegate(TreeFilterHandler<TSharedPtr<ITraceObject>>::FOnGetChildren::CreateLambda([](TSharedPtr<ITraceObject> InParent, TArray<TSharedPtr<ITraceObject>>& OutChildren)
	{
		InParent->GetChildren(OutChildren);
	}));
}

TSharedRef<ITableRow> STraceDataFilterWidget::OnGenerateRow(TSharedPtr<ITraceObject> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STraceObjectRowWidget, OwnerTable, InItem)
		.HighlightText(SearchBoxWidgetFilter.Get(), &TTextFilter<TSharedPtr<ITraceObject>>::GetRawFilterText);
}

TSharedRef<SWidget> STraceDataFilterWidget::MakeAddFilterMenu()
{
	return FilterPresetsListWidget->ExternalMakeFilterPresetsMenu();
}

TSharedPtr<SWidget> STraceDataFilterWidget::OnContextMenuOpening() const
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr, nullptr, /*bCloseSelfOnly=*/true);

	static const FName FilteringSectionHook("FilteringState");
	MenuBuilder.BeginSection(FilteringSectionHook, LOCTEXT("FilteringSectionLabel", "Filtering"));
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("EnableAllRowsLabel", "Enable All"), LOCTEXT("EnableAllRowsTooltip", "Sets entire hierarchy to be non-filtered."), FSlateIcon(), 
			FUIAction(FExecuteAction::CreateLambda([this]()
				{
					EnumerateAllItems([this](TSharedPtr<ITraceObject> Object) -> void
					{
						Object->SetIsFiltered(false);
					});
				}),
				FCanExecuteAction(),
				FGetActionCheckState(),
				FIsActionButtonVisible::CreateLambda([this]() -> bool
				{
					return Treeview->GetNumItemsSelected() == 0 && EnumerateAllItems(TFunction<bool(TSharedPtr<ITraceObject> InItem)>([this](TSharedPtr<ITraceObject> Object) -> bool
					{
						return Object->IsFiltered();
					}));
				})
			)
		);

		MenuBuilder.AddMenuEntry(LOCTEXT("DisableAllRowsLabel", "Disable All"), LOCTEXT("DisableAllRowsTooltip", "Sets entire hierarchy to be filtered."), FSlateIcon(), 
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					EnumerateAllItems([this](TSharedPtr<ITraceObject> Object) -> void
					{
						Object->SetIsFiltered(true);
					});
				}),
				FCanExecuteAction(),
				FGetActionCheckState(),
				FIsActionButtonVisible::CreateLambda([this]() -> bool
				{
					return Treeview->GetNumItemsSelected() == 0 && EnumerateAllItems(TFunction<bool(TSharedPtr<ITraceObject> InItem)>([this](TSharedPtr<ITraceObject> Object) -> bool
					{
						return !Object->IsFiltered();
					}));
				})

			)
		);

		MenuBuilder.AddMenuEntry(LOCTEXT("EnableRowsLabel", "Enable Selected"), LOCTEXT("EnableRowsTooltip", "Sets the selected Node(s) to be non-filtered."), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateRaw(this, &STraceDataFilterWidget::EnumerateSelectedItems, TFunction<void(TSharedPtr<ITraceObject> InItem)>([this](TSharedPtr<ITraceObject> Object) -> void
				{
					Object->SetIsFiltered(false);

					SetExpansionRecursively(Object, true);
				})),
				FCanExecuteAction(),
				FGetActionCheckState(),
				FIsActionButtonVisible::CreateLambda([this]() -> bool
				{
					return EnumerateSelectedItems(TFunction<bool(TSharedPtr<ITraceObject> InItem)>([this](TSharedPtr<ITraceObject> Object) -> bool
					{
						return Object->IsFiltered();
					}));
				})
			)
		);

		MenuBuilder.AddMenuEntry(LOCTEXT("DisableRowsLabel", "Disable Selected"), LOCTEXT("DisableRowsTooltip", "Sets the selected Node(s) to be filtered."), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateRaw(this, &STraceDataFilterWidget::EnumerateSelectedItems, TFunction<void(TSharedPtr<ITraceObject> InItem)>([this](TSharedPtr<ITraceObject> Object) -> void
				{
					Object->SetIsFiltered(true);
				})),
				FCanExecuteAction(),
				FGetActionCheckState(),
				FIsActionButtonVisible::CreateLambda([this]() -> bool
				{
					return EnumerateSelectedItems(TFunction<bool(TSharedPtr<ITraceObject> InItem)>([this](TSharedPtr<ITraceObject> Object) -> bool
					{
						return !Object->IsFiltered();
					}));
				})
			)
		);
	}
	MenuBuilder.EndSection();

	static const FName ExpansionSectionHook("ExpansionState");
	MenuBuilder.BeginSection(ExpansionSectionHook, LOCTEXT("ExpansionSectionLabel", "Expansion"));
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("ExpandAllRowsLabel", "Expand All"), LOCTEXT("ExpandAllRowsTooltip", "Expands the entire hierarchy."), FSlateIcon(), FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					for (const TSharedPtr<ITraceObject>& Object : TreeItems)
					{
						SetExpansionRecursively(Object, true);
					}
				}),
				FCanExecuteAction(),
				FGetActionCheckState(),
				FIsActionButtonVisible::CreateLambda([this]() -> bool
				{
					return Treeview->GetNumItemsSelected() == 0 && EnumerateAllItems(TFunction<bool(TSharedPtr<ITraceObject> InItem)>([this](TSharedPtr<ITraceObject> Object) -> bool
					{
						return !Treeview->IsItemExpanded(Object);
					}));
				})
			)
		);
		
		MenuBuilder.AddMenuEntry(LOCTEXT("CollapseAllRowsLabel", "Collapse All"), LOCTEXT("CollapseAllRowsTooltip", "Collapses the entire hierarchy."), FSlateIcon(), 
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					for (const TSharedPtr<ITraceObject>& Object : TreeItems)
					{
						SetExpansionRecursively(Object, false);
					}
				}),
				FCanExecuteAction(),
				FGetActionCheckState(),
				FIsActionButtonVisible::CreateLambda([this]() -> bool
				{
					return Treeview->GetNumItemsSelected() == 0 && EnumerateAllItems(TFunction<bool(TSharedPtr<ITraceObject> InItem)>([this](TSharedPtr<ITraceObject> Object) -> bool
					{
						return Treeview->IsItemExpanded(Object);
					}));
				})
			)
		);

		MenuBuilder.AddMenuEntry(LOCTEXT("ExpandRowsLabel", "Expand Selected"), LOCTEXT("ExpandRowsTooltip", "Expands the selected Node(s)."), FSlateIcon(), 
			FUIAction(
				FExecuteAction::CreateRaw(this, &STraceDataFilterWidget::EnumerateSelectedItems, TFunction<void(TSharedPtr<ITraceObject> InItem)>([this](TSharedPtr<ITraceObject> Object) -> void
				{
					SetExpansionRecursively(Object, true);
				})),
				FCanExecuteAction(),
				FGetActionCheckState(),
				FIsActionButtonVisible::CreateLambda([this]() -> bool
				{
					return EnumerateSelectedItems(TFunction<bool(TSharedPtr<ITraceObject> InItem)>([this](TSharedPtr<ITraceObject> Object) -> bool
					{
						return !Treeview->IsItemExpanded(Object);
					}));
				})
			)
		);

		MenuBuilder.AddMenuEntry(LOCTEXT("CollapseRowsLabel", "Collapse Selected"), LOCTEXT("CollapseRowsTooltip", "Collapse the selected Node(s)."), FSlateIcon(), 
			FUIAction(
				FExecuteAction::CreateRaw(this, &STraceDataFilterWidget::EnumerateSelectedItems, TFunction<void(TSharedPtr<ITraceObject> InItem)>([this](TSharedPtr<ITraceObject> Object) -> void
				{
					SetExpansionRecursively(Object, false);
				})),
				FCanExecuteAction(),
				FGetActionCheckState(),
				FIsActionButtonVisible::CreateLambda([this]() -> bool
				{
					return EnumerateSelectedItems(TFunction<bool(TSharedPtr<ITraceObject> InItem)>([this](TSharedPtr<ITraceObject> Object) -> bool
					{
						return Treeview->IsItemExpanded(Object);
					}));
				})
			)
		);
	}  	
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void STraceDataFilterWidget::SaveItemsExpansion()
{
	ExpandedObjectNames.Empty();

	if (Treeview.IsValid())
	{
		TSet<TSharedPtr<ITraceObject>> ExpandedItems;
		Treeview->GetExpandedItems(ExpandedItems);

		for (TSharedPtr<ITraceObject> Item : ExpandedItems)
		{
			if (Item.IsValid())
			{
				ExpandedObjectNames.Add(Item->GetName());
			}
		}
	}
}

void STraceDataFilterWidget::RestoreItemsExpansion()
{
	EnumerateAllItems([this](TSharedPtr<ITraceObject> Object) -> void
	{
		const bool bExpanded = ExpandedObjectNames.Contains(Object->GetName());
		SetExpansionRecursively(Object, bExpanded);
	});

	ExpandedObjectNames.Empty();
}

void STraceDataFilterWidget::SaveItemSelection()
{
	SelectedObjectNames.Empty();

	if (Treeview.IsValid())
	{
		EnumerateSelectedItems([this](TSharedPtr<ITraceObject> InObject)
		{
			if (InObject.IsValid())
			{
				SelectedObjectNames.Add(InObject->GetName());
			}
		});
	}
}

void STraceDataFilterWidget::RestoreItemSelection()
{
	if (Treeview.IsValid())
	{
		TArray<TSharedPtr<ITraceObject>> SelectedItems;
		EnumerateAllItems([this, &SelectedItems](TSharedPtr<ITraceObject> Object) -> void
		{
			if (SelectedObjectNames.Contains(Object->GetName()))
			{
				SelectedItems.Add(Object);
			}
		});

		Treeview->SetItemSelection(SelectedItems, true);
	}
	
	SelectedObjectNames.Empty();
}

void STraceDataFilterWidget::SetCurrentAnalysisSession(Trace::FSessionHandle Handle)
{
	SessionFilterService = FilterService.GetFilterServiceByHandle(Handle);
	/** Refresh presets so config loaded state is directly applied */
	OnPresetsChanged();
	/** Refresh data driving the treeview */
	RefreshTreeviewData();
}

TSharedRef<ITraceObject> STraceDataFilterWidget::AddFilterableObject(const FTraceObjectInfo& Event, FString ParentName)
{
	// Retrieve any child objects, and recursively add those first 
	TArray<FTraceObjectInfo> Events;
	SessionFilterService->GetChildObjects(Event.Hash, Events);

	TArray<TSharedPtr<ITraceObject>> ChildPtrs;	
	for (const FTraceObjectInfo& ChildEvent : Events)
	{
		TSharedRef<ITraceObject> EventItem = AddFilterableObject(ChildEvent, Event.Name);
		ChildPtrs.Add(EventItem);
	}

	TSharedRef<FTraceChannel> SharedItem = MakeShareable(new FTraceChannel(Event.Name, ParentName, Event.Hash, !Event.bEnabled, ChildPtrs, SessionFilterService));
	ParentToChild.Add(SharedItem, ChildPtrs);

	for (TSharedPtr<ITraceObject> ChildObject : ChildPtrs)
	{
		ChildToParent.Add(ChildObject, SharedItem);
	}

	FlatItems.Add(SharedItem);

	return SharedItem;
}

bool STraceDataFilterWidget::HasValidFilterSession() const
{
	return SessionFilterService.IsValid();
}

void STraceDataFilterWidget::SetExpansionRecursively(const TSharedPtr<ITraceObject>& InObject, bool bShouldExpandItem) const
{
	Treeview->SetItemExpansion(InObject, bShouldExpandItem);

	TArray<TSharedPtr<ITraceObject>> Children;
	InObject->GetChildren(Children);

	for (TSharedPtr<ITraceObject>& ChildObject : Children)
	{
		SetExpansionRecursively(ChildObject, bShouldExpandItem);
	}
}

void STraceDataFilterWidget::SetParentExpansionRecursively(const TSharedPtr<ITraceObject>& InObject, bool bShouldExpandItem) const
{
	if (const TSharedPtr<ITraceObject>* ParentObject = ChildToParent.Find(InObject))
	{
		Treeview->SetItemExpansion(*ParentObject, bShouldExpandItem);
		SetParentExpansionRecursively(*ParentObject, bShouldExpandItem);
	}
}

void STraceDataFilterWidget::EnumerateSelectedItems(TFunction<void(TSharedPtr<ITraceObject> InItem)> InFunction) const
{
	TArray<TSharedPtr<ITraceObject>> SelectedObjects;
	Treeview->GetSelectedItems(SelectedObjects);

	for (const TSharedPtr<ITraceObject>& Object : SelectedObjects)
	{
		InFunction(Object);
	}
}

bool STraceDataFilterWidget::EnumerateSelectedItems(TFunction<bool(TSharedPtr<ITraceObject> InItem)> InFunction) const
{
	TArray<TSharedPtr<ITraceObject>> SelectedObjects;
	Treeview->GetSelectedItems(SelectedObjects);


	bool bState = false;
	for (const TSharedPtr<ITraceObject>& Object : SelectedObjects)
	{
		bState |= InFunction(Object);
	}

	return bState;
}

void STraceDataFilterWidget::EnumerateAllItems(TFunction<void(TSharedPtr<ITraceObject> InItem)> InFunction) const
{
	for (const TSharedPtr<ITraceObject>& Object : FlatItems)
	{
		InFunction(Object);
	}
}

bool STraceDataFilterWidget::EnumerateAllItems(TFunction<bool(TSharedPtr<ITraceObject> InItem)> InFunction) const
{
	bool bState = false;
	for (const TSharedPtr<ITraceObject>& Object : FlatItems)
	{
		bState |= InFunction(Object);
	}
	return bState;
}

void STraceDataFilterWidget::RefreshTreeviewData()
{
	if (SessionFilterService.IsValid())
	{
		SyncTimeStamp = SessionFilterService->GetTimestamp();

		/** Save expansion and selection */
		SaveItemsExpansion();
		SaveItemSelection();

		TArray<FTraceObjectInfo> RootEvents;
		SessionFilterService->GetRootObjects(RootEvents);
		
		ParentToChild.Empty();
		ChildToParent.Empty();
		RootItems.Empty();		

		for (const FTraceObjectInfo& RootEvent : RootEvents)
		{
			TSharedRef<ITraceObject> TraceObject = AddFilterableObject(RootEvent, FString());
			RootItems.Add(TraceObject);
		}

		TreeviewFilterHandler->RefreshAndFilterTree();

		RestoreItemsExpansion();
		RestoreItemSelection();
	}
}

void STraceDataFilterWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (SessionFilterService.IsValid() )
	{
		if (SessionFilterService->GetTimestamp() != SyncTimeStamp)
		{
			RefreshTreeviewData();
		}
	}
	else
	{
		ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");
		TArray<Trace::FSessionHandle> LiveSessionHandles;
		TraceServicesModule.GetSessionService()->GetLiveSessions(LiveSessionHandles);
		for (const Trace::FSessionHandle& Handle : LiveSessionHandles)
		{
			SetCurrentAnalysisSession(Handle);
			break;
		}
	}

	if (bNeedsTreeRefresh)
	{
		TreeviewFilterHandler->RefreshAndFilterTree();
	}
}


#undef LOCTEXT_NAMESPACE // "STraceDataFilterWidget"