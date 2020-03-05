// Copyright Epic Games, Inc. All Rights Reserved.

#include "STraceSourceFilteringWidget.h"
#include "EditorStyleSet.h"

#include "UObject/SoftObjectPtr.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Widgets/Input/SComboButton.h"
#include "Algo/Transform.h"
#include "Widgets/Layout/SSeparator.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Images/SThrobber.h"
#include "Modules/ModuleManager.h"
#include "TraceServices/ITraceServicesModule.h"
#include "UObject/ObjectMacros.h"
#include "Misc/EnumRange.h"

#include "Insights/IUnrealInsightsModule.h"
#include "Trace/StoreClient.h"

#include "SSourceFilteringTreeview.h"
#include "SourceFilterStyle.h"
#include "SFilterObjectWidget.h"
#include "TraceSourceFilteringSettings.h"
#include "SWorldTraceFilteringWidget.h"
#include "IDataSourceFilterSetInterface.h"
#include "TreeViewBuilder.h"

#if WITH_EDITOR
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Engine/Blueprint.h"
#include "EmptySourceFilter.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "STraceSourceFilteringWidget"

STraceSourceFilteringWidget::STraceSourceFilteringWidget() : FilteringSettings(nullptr)
{
}

STraceSourceFilteringWidget::~STraceSourceFilteringWidget()
{
	SaveFilteringSettings();
}

void STraceSourceFilteringWidget::Construct(const FArguments& InArgs)
{
	ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");

#if WITH_EDITOR
	ConstructInstanceDetailsView();
#endif // WITH_EDITOR

	ConstructTreeview();
	ConstructMenuBox();
	
	ChildSlot
	[
		SNew(SBorder)
		.Padding(4.0f)
		.BorderImage(FSourceFilterStyle::GetBrush("SourceFilter.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 2.0f))
			[			
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					MenuBox->AsShared()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SThrobber)
					.Visibility(this, &STraceSourceFilteringWidget::GetThrobberVisibility)
				]
			]
			+ SVerticalBox::Slot()
			[	
				SNew(SSplitter)
				.Orientation(EOrientation::Orient_Vertical)
				.Style(FSourceFilterStyle::Get(), "SourceFilter.Splitter")
				.PhysicalSplitterHandleSize(2.0f)
				+ SSplitter::Slot()
				.Value(.5f)
				[
					SNew(SBox)
					.Padding(2.0f)
					[
						FilterTreeView->AsShared()						
					]
				]
#if WITH_EDITOR
				+ SSplitter::Slot()
				.Value(.5f)
				[	
					SNew(SSplitter)
					.Orientation(EOrientation::Orient_Horizontal)
					.Style(FSourceFilterStyle::Get(), "SourceFilter.Splitter")
					.PhysicalSplitterHandleSize(2.0f)
					+ SSplitter::Slot()
					.Value(.5f)
					[
						SNew(SBox)
						.Padding(2.0f)
						[
							FilterInstanceDetailsView->AsShared()
						]
					]				
#endif // WITH_EDITOR
					+ SSplitter::Slot()
					.Value(.5f)
					[	
						SNew(SBox)
						.Padding(2.0f)
						[
							SAssignNew(WorldFilterWidget, SWorldTraceFilteringWidget)
						]
					]
#if WITH_EDITOR
				]
#endif // WITH_EDITOR
			]	
		]
	];

	TAttribute<bool> EnabledAttribute = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &STraceSourceFilteringWidget::ShouldWidgetsBeEnabled));
	FilterTreeView->SetEnabled(EnabledAttribute);
	MenuBox->SetEnabled(EnabledAttribute);

#if WITH_EDITOR
	FilterInstanceDetailsView->SetEnabled(EnabledAttribute);
#endif // WITH_EDITOR
}

void STraceSourceFilteringWidget::ConstructMenuBox()
{
	/** Callback for whenever a Filter class (name) was selected */
	auto OnFilterClassPicked = [this](FString PickedFilterName)
	{
		if (SessionFilterService.Get())
		{
			SessionFilterService->AddFilter(PickedFilterName);
			AddFilterButton->SetIsOpen(false);
		}
	};

	SAssignNew(MenuBox, SHorizontalBox)
	+SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		SAssignNew(AddFilterButton, SComboButton)
		.Visibility(EVisibility::Visible)
		.ComboButtonStyle(FSourceFilterStyle::Get(), "SourceFilter.ComboButton")
		.ForegroundColor(FLinearColor::White)
		.ContentPadding(0.0f)
		.OnGetMenuContent(FOnGetContent::CreateLambda([OnFilterClassPicked, this]()
		{
			FMenuBuilder MenuBuilder(true, TSharedPtr<FUICommandList>(), SessionFilterService->GetExtender());
			
			MenuBuilder.BeginSection(FName("FilterPicker"));
			{
				MenuBuilder.AddWidget(SessionFilterService->GetFilterPickerWidget(FOnFilterClassPicked::CreateLambda(OnFilterClassPicked)), FText::GetEmpty(), true, false);
			}
			MenuBuilder.EndSection();
			

			return MenuBuilder.MakeWidget();
		}))
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(FSourceFilterStyle::Get(), "SourceFilter.TextStyle")
				.Font(FSourceFilterStyle::Get().GetFontStyle("FontAwesome.9"))
				.Text(FText::FromString(FString(TEXT("\xf0fe"))) /*fa-filter*/)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0, 0, 0)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(FSourceFilterStyle::Get(), "SourceFilter.TextStyle")
				.Text(LOCTEXT("FilterMenuLabel", "Add Filter"))
			]
		]
	]

	+SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(2.0f, 0.0f, 0.0f, 0.0f)
	[
		SNew(SComboButton)
		.Visibility(EVisibility::Visible)
		.ComboButtonStyle(FSourceFilterStyle::Get(), "SourceFilter.ComboButton")
		.ForegroundColor(FLinearColor::White)
		.ContentPadding(0.0f)
		.OnGetMenuContent(this, &STraceSourceFilteringWidget::OnGetOptionsMenu)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(FSourceFilterStyle::Get(), "SourceFilter.TextStyle")
				.Font(FSourceFilterStyle::Get().GetFontStyle("FontAwesome.9"))
				.Text(FText::FromString(FString(TEXT("\xf0b0"))) /*fa-filter*/)
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0, 0, 0)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.TextStyle(FSourceFilterStyle::Get(), "SourceFilter.TextStyle")
				.Text(LOCTEXT("OptionMenuLabel", "Options"))
			]
		]
	];
}

void STraceSourceFilteringWidget::ConstructTreeview()
{
	SAssignNew(FilterTreeView, SSourceFilteringTreeView, StaticCastSharedRef<STraceSourceFilteringWidget>(AsShared()))
	.ItemHeight(20.0f)
	.TreeItemsSource(&FilterObjects)
	.OnGetChildren_Lambda([this](TSharedPtr<IFilterObject> InObject, TArray<TSharedPtr<IFilterObject>>& OutChildren)
	{
		if (TArray<TSharedPtr<IFilterObject>> * ChildArray = ParentToChildren.Find(InObject))
		{
			OutChildren.Append(*ChildArray);
		}
	})
	.OnGenerateRow_Lambda([](TSharedPtr<IFilterObject> InItem, const TSharedRef<STableViewBase>& OwnerTable)
	{
		return SNew(SFilterObjectRowWidget, OwnerTable, InItem);
	})
#if WITH_EDITOR
	.OnSelectionChanged_Lambda([this](TSharedPtr<IFilterObject> InItem, ESelectInfo::Type SelectInfo) -> void
	{
		if (InItem.IsValid())
		{
			UObject* Filter = InItem->GetFilter();
			FilterInstanceDetailsView->SetObject(Filter);
		}
		else
		{
			FilterInstanceDetailsView->SetObject(nullptr);
		}	
	})
#endif // WITH_EDITOR
	.OnContextMenuOpening(this, &STraceSourceFilteringWidget::OnContextMenuOpening);
}

#if WITH_EDITOR
void STraceSourceFilteringWidget::ConstructInstanceDetailsView()
{
	// Create a property view
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs(
		/*bUpdateFromSelection=*/ false,
		/*bLockable=*/ false,
		/*bAllowSearch=*/ false,
		FDetailsViewArgs::HideNameArea,
		/*bHideSelectionTip=*/ true,
		/*InNotifyHook=*/ nullptr,
		/*InSearchInitialKeyFocus=*/ false,
		/*InViewIdentifier=*/ NAME_None);
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
	DetailsViewArgs.bShowOptions = false;	

	FilterInstanceDetailsView = EditModule.CreateDetailView(DetailsViewArgs);
}
#endif // WITH_EDITOR

void STraceSourceFilteringWidget::SetCurrentAnalysisSession(uint32 SessionHandle, TSharedRef<const Trace::IAnalysisSession> AnalysisSession)
{
	SessionFilterService = FSourceFilterService::GetFilterServiceForSession(SessionHandle, AnalysisSession);

	WorldFilterWidget->SetSessionFilterService(SessionFilterService);

	RefreshFilteringData();
}

bool STraceSourceFilteringWidget::HasValidFilterSession() const
{
	return SessionFilterService.IsValid();
}

EVisibility STraceSourceFilteringWidget::GetThrobberVisibility() const
{
	if (HasValidFilterSession())
	{
		return SessionFilterService->IsActionPending() ? EVisibility::Visible : EVisibility::Hidden;
	}

	return EVisibility::Hidden;
}

bool STraceSourceFilteringWidget::ShouldWidgetsBeEnabled() const
{
	if (HasValidFilterSession())
	{
		return !SessionFilterService->IsActionPending();
	}

	return false;
}

void STraceSourceFilteringWidget::RefreshFilteringData()
{
	SaveTreeviewState();

	FilterObjects.Empty();
	ParentToChildren.Empty();
	FlatFilterObjects.Empty();

	FTreeViewDataBuilder Builder(FilterObjects, ParentToChildren, FlatFilterObjects);
	SessionFilterService->PopulateTreeView(Builder);

	FilterTreeView->RequestTreeRefresh();

	FilteringSettings = SessionFilterService->GetFilterSettings();

	RestoreTreeviewState();
}

void STraceSourceFilteringWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (SessionFilterService.IsValid() )
	{
		const FDateTime& Stamp = SessionFilterService->GetTimestamp();
		
		if (Stamp != SyncTimestamp)
		{
			RefreshFilteringData();
			SyncTimestamp = Stamp;
		}
	}
	else
	{
		IUnrealInsightsModule& InsightsModule = FModuleManager::LoadModuleChecked<IUnrealInsightsModule>("TraceInsights");
		TSharedPtr<const Trace::IAnalysisSession> AnalysisSession = InsightsModule.GetAnalysisSession();
		if (AnalysisSession.IsValid())
		{
			Trace::FStoreClient* StoreClient = InsightsModule.GetStoreClient();
			const int32 SessionCount = StoreClient->GetSessionCount();

			if (SessionCount > 0)
			{
				const Trace::FStoreClient::FSessionInfo* SessionInfo = StoreClient->GetSessionInfo(SessionCount - 1);
				if (SessionInfo)
				{
					SetCurrentAnalysisSession(SessionInfo->GetTraceId(), AnalysisSession.ToSharedRef());
				}
			}
		}
	}
}

TSharedRef<SWidget> STraceSourceFilteringWidget::OnGetOptionsMenu()
{
	FMenuBuilder Builder(true, TSharedPtr<FUICommandList>(), SessionFilterService->GetExtender());
	
	if (FilteringSettings)
	{
		Builder.BeginSection(NAME_None, LOCTEXT("VisualizationSectionLabel", "Visualization"));
		{
			Builder.AddSubMenu(LOCTEXT("VisualizeLabel", "Visualize"),
				LOCTEXT("DebugDrawingTooltip", "Sub menu related to Debug Drawing"),
				FNewMenuDelegate::CreateLambda([this](FMenuBuilder& InMenuBuilder)
					{
						InMenuBuilder.AddMenuEntry(
							LOCTEXT("DrawFilterStateLabel","Actor Filtering"), 
							LOCTEXT("DrawFilteringStateTooltip", "Draws the bounding box for each filter processed Actor in the world."),
							FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateLambda([this]() -> void
								{
									FilteringSettings->bDrawFilteringStates = !FilteringSettings->bDrawFilteringStates;
									SessionFilterService->UpdateFilterSettings(FilteringSettings);
								}),
								FCanExecuteAction::CreateLambda([this]() -> bool
								{
									return FilteringSettings != nullptr;
								}),
								FGetActionCheckState::CreateLambda([this]() -> ECheckBoxState
								{
									return FilteringSettings->bDrawFilteringStates ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								})
							),
							NAME_None,
							EUserInterfaceActionType::ToggleButton
						);

						InMenuBuilder.AddMenuEntry(
							LOCTEXT("DrawFilterPassingOnlyLabel","Only Actor(s) passing Filtering"), 
							LOCTEXT("DrawFilterPassingOnlyTooltip", "Only draws the filtering state for Actors that passsed the filtering state."),
							FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateLambda([this]()
								{
									FilteringSettings->bDrawOnlyPassingActors = !FilteringSettings->bDrawOnlyPassingActors;
									SessionFilterService->UpdateFilterSettings(FilteringSettings);
								}),
								FCanExecuteAction::CreateLambda([this]()
								{
									return FilteringSettings && FilteringSettings->bDrawFilteringStates;
								}),
								FGetActionCheckState::CreateLambda([this]()
								{
									return FilteringSettings->bDrawOnlyPassingActors ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								})
							),
							NAME_None,
							EUserInterfaceActionType::ToggleButton
						);

						InMenuBuilder.AddMenuEntry(
							LOCTEXT("DrawNonPassingFiltersLabel","Draw non passing Filters"), 
							LOCTEXT("DrawNonPassingFiltersTooltip", "Draws the Filters that caused an Actor to be filtered out."),
							FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateLambda([this]()
								{
									FilteringSettings->bDrawFilterDescriptionForRejectedActors = !FilteringSettings->bDrawFilterDescriptionForRejectedActors;
									SessionFilterService->UpdateFilterSettings(FilteringSettings);
								}),
								FCanExecuteAction::CreateLambda([this]()
								{
									return FilteringSettings && FilteringSettings->bDrawFilteringStates;
								}),
								FGetActionCheckState::CreateLambda([this]()
								{
									return FilteringSettings->bDrawFilterDescriptionForRejectedActors ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								})
							),
							NAME_None,
							EUserInterfaceActionType::ToggleButton
						);
					
					}
				),
				false,
				FSlateIcon(),
				false
			);
		}
		Builder.EndSection();
	}
	

	
	const FName SectionName = TEXT("FilterOptionsMenu");
	Builder.BeginSection(SectionName, LOCTEXT("FiltersSectionLabel", "Filters"));
	{
		Builder.AddMenuEntry(
			LOCTEXT("ResetFiltersLabel","Reset Filters"), 
			LOCTEXT("ResetFiltersTooltip", "Removes all currently set filters."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					SessionFilterService->ResetFilters();
				}),
				FCanExecuteAction::CreateLambda([this]()
				{
					return FilterObjects.Num() > 0;
				})
			)
		);	
	}
	Builder.EndSection();

	return Builder.MakeWidget();	
}


TSharedPtr<SWidget> STraceSourceFilteringWidget::OnContextMenuOpening()
{
	/** Selection information */
	TArray<TSharedPtr<IFilterObject>> FilterSelection;
	FilterTreeView->GetSelectedItems(FilterSelection);
	
	FMenuBuilder MenuBuilder(true, TSharedPtr<const FUICommandList>(), SessionFilterService->GetExtender());

	if (FilterSelection.Num() > 0)
	{
		bool bSelectionContainsFilterSet = false;
		bool bSelectionContainsBPFilter = false;
		bool bSelectionContainsEmptyFilter = false;
		bool bSelectionContainsNonEmptyFilter = false;
		const bool bMultiSelection = FilterSelection.Num() > 1;

		/** Gather information about current filter selection set */
		for (const TSharedPtr<IFilterObject>& Filter : FilterSelection)
		{
			const UObject* FilterObject = Filter->GetFilter();
			if (ensure(FilterObject))
			{
				if (const IDataSourceFilterSetInterface* SetInterface = Cast<IDataSourceFilterSetInterface>(FilterObject))
				{
					bSelectionContainsFilterSet = true;
				}

#if WITH_EDITOR
				if (const UEmptySourceFilter* EmptyFilter = Cast<UEmptySourceFilter>(FilterObject))
				{
					bSelectionContainsEmptyFilter = true;
				}
				else
				{
					bSelectionContainsNonEmptyFilter = true;
				}
#else
				if (const UTraceDataSourceFilter* TraceFilter = Cast<UTraceDataSourceFilter>(FilterObject))
				{
					const bool bIsEmptyFilter = (TraceFilter->ClassName == TEXT("EmptySourceFilter"));
					bSelectionContainsEmptyFilter |= bIsEmptyFilter;
					bSelectionContainsNonEmptyFilter |= !bIsEmptyFilter;
				}
#endif // WITH_EDITOR

				if (FilterObject->GetClass()->ClassGeneratedBy != nullptr)
				{
					bSelectionContainsBPFilter = true;
				}
			}
		}

#if WITH_EDITOR
		if (bSelectionContainsBPFilter)
		{
			MenuBuilder.BeginSection(NAME_None, LOCTEXT("BlueprintFilterSectionLabel", "Blueprint Filter"));
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("OpenFilterLabel", "Open Filter Blueprint"),
					LOCTEXT("OpenFilterTooltip", "Opens this Filter's Blueprint."),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.Blueprint"),
					FUIAction(
						FExecuteAction::CreateLambda([FilterSelection]()
						{
							for (const TSharedPtr<IFilterObject>& FilterObject : FilterSelection)
							{
								const UObject* FilterUObject = FilterObject->GetFilter();
								if (ensure(FilterUObject))
								{
									if (UBlueprint* Blueprint = Cast<UBlueprint>(FilterUObject->GetClass()->ClassGeneratedBy))
									{
										GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
									}
								}
							}
						})
					)
				);
			}
			MenuBuilder.EndSection();

		}
#endif // WITH_EDITOR

		/** Single selection of a filter set */
		if (bSelectionContainsFilterSet && !bMultiSelection)
		{
			auto AddFilterToSet = [this, FilterSelection](FString ClassName)
			{
				if (SessionFilterService.Get())
				{
					SessionFilterService->AddFilterToSet(FilterSelection[0]->AsShared(), ClassName);
				}

				FSlateApplication::Get().DismissAllMenus();
			};

			MenuBuilder.BeginSection(NAME_None, LOCTEXT("FilterSetContextMenuLabel", "Filter Set"));
			{
				MenuBuilder.AddSubMenu(
					LOCTEXT("AddFilterToSetLabel", "Add Filter"),
					LOCTEXT("AddFilterToSetTooltip", "Adds a filter to this Filtering Set."),
					FNewMenuDelegate::CreateLambda([this, AddFilterToSet](FMenuBuilder& InSubMenuBuilder)
					{
						InSubMenuBuilder.AddWidget(
							SessionFilterService->GetFilterPickerWidget(FOnFilterClassPicked::CreateLambda(AddFilterToSet)),
							FText::GetEmpty(),
							true
						);
					})
				);
			}
			MenuBuilder.EndSection();
		}

		MenuBuilder.BeginSection(NAME_None, LOCTEXT("FiltersContextMenuLabel", "Filter"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("EnableFilterLabel", "Filter Enabled"),
				LOCTEXT("ToggleFilterTooltips", "Sets whether or not this Filter should be considered when applying the set of filters"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([FilterSelection, this]()
					{
						bool bEnabled = false;
						bool bDisabled = false;

						for (const TSharedPtr<IFilterObject>& FilterObject : FilterSelection)
						{
							bEnabled |= FilterObject->IsFilterEnabled();
							bDisabled |= !FilterObject->IsFilterEnabled();
						}

						for (const TSharedPtr<IFilterObject>& FilterObject : FilterSelection)
						{
							SessionFilterService->SetFilterState(FilterObject->AsShared(), (bEnabled && bDisabled) || bDisabled);
						}
					}),
					FCanExecuteAction(),
					FGetActionCheckState::CreateLambda([FilterSelection]()
					{
						bool bEnabled = false;
						bool bDisabled = false;

						for (const TSharedPtr<IFilterObject>& Filter : FilterSelection)
						{
							bEnabled |= Filter->IsFilterEnabled();
							bDisabled |= !Filter->IsFilterEnabled();
						}

						return bEnabled ? (bDisabled ? ECheckBoxState::Undetermined : ECheckBoxState::Checked) : ECheckBoxState::Unchecked;
					})
				),
				NAME_None,
				EUserInterfaceActionType::Check
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("RemoveFilterLabel", "Remove Filter"),
				LOCTEXT("RemoveFilterTooltip", "Removes this Filter from the filtering set."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, FilterSelection]()
					{
						for (const TSharedPtr<IFilterObject>& FilterObject : FilterSelection)
						{
							SessionFilterService->RemoveFilter(FilterObject->AsShared());
						}
					})
				)
			);
		}
		MenuBuilder.EndSection();

		/** Single selection of a valid Filter instance */
		if (!bMultiSelection && bSelectionContainsNonEmptyFilter)
		{
			MenuBuilder.BeginSection(NAME_None, LOCTEXT("AddFilterSetSectionLabel", "Add Filter Set"));
			{
				FText LabelTextFormat = LOCTEXT("MakeFilterSetLabel", "{0}");
				FText ToolTipTextFormat = LOCTEXT("MakeFilterSetTooltip", "Creates a new filter set, containing this filter, with the {0} operator");

				const UEnum* EnumPtr = FindObject<UEnum>(ANY_PACKAGE, TEXT("EFilterSetMode"), true);
				for (EFilterSetMode Mode : TEnumRange<EFilterSetMode>())
				{
					FText ModeText = EnumPtr->GetDisplayNameTextByValue((int64)Mode);
					MenuBuilder.AddMenuEntry(
						FText::Format(LabelTextFormat, ModeText),
						FText::Format(ToolTipTextFormat, ModeText),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([this, Mode, FilterSelection]()
							{
								SessionFilterService->MakeFilterSet(FilterSelection[0]->AsShared(), Mode);
							})
						)
					);
				}
			}
			MenuBuilder.EndSection();
		}
	}
	else
	{
		auto OnFilterClassPicked = [this](FString PickedFilterName)
		{
			if (SessionFilterService.Get())
			{
				SessionFilterService->AddFilter(PickedFilterName);
				AddFilterButton->SetIsOpen(false);
			}
		};
		
		MenuBuilder.AddWidget(SessionFilterService->GetFilterPickerWidget(FOnFilterClassPicked::CreateLambda(OnFilterClassPicked)), FText::GetEmpty(), true, false);
	}

	return MenuBuilder.MakeWidget();
}

void STraceSourceFilteringWidget::SaveFilteringSettings()
{
	if (FilteringSettings)
	{
		FilteringSettings->SaveConfig();
	}
}

void STraceSourceFilteringWidget::SaveTreeviewState()
{
	if (FilterTreeView)
	{
		ensure(ExpandedFilters.Num() == 0);
		TSet<TSharedPtr<IFilterObject>> TreeviewExpandedObjects;
		FilterTreeView->GetExpandedItems(TreeviewExpandedObjects);
		Algo::Transform(TreeviewExpandedObjects, ExpandedFilters, [](TSharedPtr<IFilterObject> Object)
		{
			return GetTypeHash(Object->GetFilter());
		});

		ensure(SelectedFilters.Num() == 0);
		TArray<TSharedPtr<IFilterObject>> TreeviewSelectedObjects;
		FilterTreeView->GetSelectedItems(TreeviewSelectedObjects);
		Algo::Transform(TreeviewSelectedObjects, SelectedFilters, [](TSharedPtr<IFilterObject> Object)
		{
			return GetTypeHash(Object->GetFilter());
		});

	}
}

void STraceSourceFilteringWidget::RestoreTreeviewState()
{
	if (FilterTreeView)
	{
		FilterTreeView->ClearExpandedItems();
		for (TSharedPtr<IFilterObject> FilterObject : FlatFilterObjects)
		{
			if (ExpandedFilters.Contains(GetTypeHash(FilterObject->GetFilter())))
			{
				FilterTreeView->SetItemExpansion(FilterObject, true);
			}
		}

		ExpandedFilters.Empty();

		FilterTreeView->ClearSelection();
		for (TSharedPtr<IFilterObject> FilterObject : FlatFilterObjects)
		{
			if (SelectedFilters.Contains(GetTypeHash(FilterObject->GetFilter())))
			{
				FilterTreeView->SetItemSelection(FilterObject, true);
			}
		}

		SelectedFilters.Empty();
	}
}


#undef LOCTEXT_NAMESPACE // "STraceSourceFilteringWidget"
