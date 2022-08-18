// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/MainPanel/SObjectMixerEditorMainPanel.h"

#include "ObjectMixerEditorLog.h"
#include "ObjectMixerEditorModule.h"
#include "ObjectMixerEditorProjectSettings.h"
#include "Views/List/ObjectMixerEditorList.h"
#include "Views/MainPanel/ObjectMixerEditorMainPanel.h"
#include "Views/Widgets/SObjectMixerPlacementAssetMenuEntry.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Kismet2/SClassPickerDialog.h"
#include "PlacementMode/Public/IPlacementModeModule.h"
#include "SPositiveActionButton.h"
#include "Styling/StyleColors.h"
#include "Views/List/ObjectMixerEditorListFilters/IObjectMixerEditorListFilter.h"
#include "Views/List/ObjectMixerEditorListFilters/ObjectMixerEditorListFilter_Category.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ObjectMixerEditor"

void SObjectMixerEditorMainPanel::Construct(
	const FArguments& InArgs, const TSharedRef<FObjectMixerEditorMainPanel>& InMainPanel)
{
	check(InMainPanel->GetEditorListModel().IsValid());

	MainPanelModel = InMainPanel;
	
	ChildSlot
	[
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.AutoHeight()
		[
			GenerateToolbar()
		]

		+SVerticalBox::Slot()
		.Padding(8, 2, 8, 7)
		.AutoHeight()
		[
			SAssignNew(CategorySelectorBox, SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(4,4))
		]

		+SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			MainPanelModel.Pin()->GetEditorListModel().Pin()->GetOrCreateWidget()
		]
	];

	InMainPanel->GetOnObjectMixerCategoryMapChanged().AddRaw(this, &SObjectMixerEditorMainPanel::RebuildCategorySelector);

	ShowFilters.Add(MakeShared<FObjectMixerEditorListFilter_Category>());
}

TSharedRef<SWidget> SObjectMixerEditorMainPanel::GenerateToolbar()
{
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
	   .OnGetMenuContent(FOnGetContent::CreateRaw(this, &SObjectMixerEditorMainPanel::OnGenerateAddObjectButtonMenu))
   ];

	ToolbarBox->AddSlot()
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Center)
	.Padding(0.f, 1.f, 0.f, 1.f)
	[
		SAssignNew(SearchBoxPtr, SSearchBox)
		.HintText(LOCTEXT("SearchHintText", "Search Scene Objects"))
		.ToolTipText(LOCTEXT("ObjectMixerEditorList_TooltipText", "Search Scene Objects"))
		.OnTextChanged_Raw(this, &SObjectMixerEditorMainPanel::OnSearchTextChanged)
	];

	// Selection Sync Toggle
	ToolbarBox->AddSlot()
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Center)
	.AutoWidth()
	.Padding(8.f, 1.f, 0.f, 1.f)
	[
		SNew(SCheckBox )
		.Padding(4.f)
		.ToolTipText(LOCTEXT("SyncSelectionButton_Tooltip", "Sync Selection\nIf enabled, clicking an item in the mixer list will also select the item in the Scene Outliner.\nAlt + Click to select items in mixer without selecting the item in the Scene outliner.\nIf disabled, selections will not sync unless Alt is held. Effectively, this is the opposite behavior."))
		.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("ToggleButtonCheckbox"))
		.ForegroundColor(FSlateColor::UseForeground())
		.IsChecked_Lambda([]()
		{
			if (const UObjectMixerEditorProjectSettings* Settings = GetDefault<UObjectMixerEditorProjectSettings>())
			{
				return Settings->bSyncSelection ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}
			
			return ECheckBoxState::Undetermined; 
		})
		.OnCheckStateChanged_Lambda([](ECheckBoxState InNewState)
		{
			if (UObjectMixerEditorProjectSettings* Settings = GetMutableDefault<UObjectMixerEditorProjectSettings>())
			{
				Settings->bSyncSelection = InNewState == ECheckBoxState::Checked ? true : false;
			}
		})
		[
			SNew(SImage)
			.ColorAndOpacity(FSlateColor::UseForeground())
			.Image( FAppStyle::Get().GetBrush("FoliageEditMode.SelectAll") )
		]
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
		.OnGetMenuContent( this, &SObjectMixerEditorMainPanel::BuildShowOptionsMenu)
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

TSharedRef<SWidget> SObjectMixerEditorMainPanel::OnGenerateAddObjectButtonMenu() const
{
	const TSet<TSubclassOf<AActor>> SubclassesOfActor = MainPanelModel.Pin()->GetObjectClassesToPlace();

	if (SubclassesOfActor.Num() > 0)
	{
		TSet<UClass*> ClassesToPlace =
		   MainPanelModel.Pin()->GetObjectFilter()->GetParentAndChildClassesFromSpecifiedClasses(
			   SubclassesOfActor,
			   MainPanelModel.Pin()->GetObjectFilter()->GetObjectMixerPlacementClassInclusionOptions());
	
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

TSharedRef<SWidget> SObjectMixerEditorMainPanel::OnGenerateFilterClassMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

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
		check(MainPanelModel.Pin());
		
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
						   FExecuteAction::CreateSP(MainPanelModel.Pin().ToSharedRef(), &FObjectMixerEditorMainPanel::SetObjectFilterClass, DerivedClass),
						   FCanExecuteAction::CreateLambda([](){ return true; }),
						   FIsActionChecked::CreateSP(MainPanelModel.Pin().ToSharedRef(), &FObjectMixerEditorMainPanel::IsClassSelected, DerivedClass)
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

TSharedRef<SWidget> SObjectMixerEditorMainPanel::BuildShowOptionsMenu()
{
	FMenuBuilder ShowOptionsMenuBuilder = FMenuBuilder(true, nullptr);

	// No need to select filter class from outside generic instance
	if (MainPanelModel.Pin()->GetModuleName() == FObjectMixerEditorModule::BaseObjectMixerModuleName)
	{
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
					.OnGetMenuContent(this, &SObjectMixerEditorMainPanel::OnGenerateFilterClassMenu)
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
	}

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
		// No need to open generic instance from itself
		if (MainPanelModel.Pin()->GetModuleName() != FObjectMixerEditorModule::BaseObjectMixerModuleName)
		{
			ShowOptionsMenuBuilder.AddMenuEntry(
			  LOCTEXT("OpenGenericInstanceMenuOption", "Open Generic Object Mixer Instance"),
			  LOCTEXT("OpenGenericInstanceMenuOptionTooltip", "Open a generic object mixer instance that can take in a user-specified filter class."),
			  FSlateIcon(),
			  FUIAction(FExecuteAction::CreateLambda([]()
			  {
				  FGlobalTabmanager::Get()->TryInvokeTab(FObjectMixerEditorModule::Get().GetTabSpawnerId());
			  })));
		}

		ShowOptionsMenuBuilder.AddMenuEntry(
			LOCTEXT("RebuildListMenuOption","Rebuild List"), 
			LOCTEXT("RebuildListMenuOptionTooltip","Force the active list to be rebuilt. Useful if the list doesn't automatically refresh."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(MainPanelModel.Pin().ToSharedRef(), &FObjectMixerEditorMainPanel::RequestRebuildList)));
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
				// Don't add non-toggle filters to Show Options, they're always on
				if (Filter->IsToggleable())
				{
					AddFiltersLambda(Filter);
				}
			}
		}
		ShowOptionsMenuBuilder.EndSection();
	}

	return ShowOptionsMenuBuilder.MakeWidget();
}

void SObjectMixerEditorMainPanel::OnSearchTextChanged(const FText& Text)
{
	ExecuteListViewSearchOnAllRows(Text.ToString(), true);
}

FString SObjectMixerEditorMainPanel::GetSearchStringFromSearchInputField() const
{
	return ensureAlwaysMsgf(SearchBoxPtr.IsValid(),
		TEXT("%hs: SearchBoxPtr is not valid. Check to make sure it was created."), __FUNCTION__)
	? SearchBoxPtr->GetText().ToString() : "";
}

void SObjectMixerEditorMainPanel::SetSearchStringInSearchInputField(const FString InSearchString) const
{
	if (ensureAlwaysMsgf(SearchBoxPtr.IsValid(),
		TEXT("%hs: SearchBoxPtr is not valid. Check to make sure it was created."), __FUNCTION__))
	{
		SearchBoxPtr->SetText(FText::FromString(InSearchString));
	}
}

void SObjectMixerEditorMainPanel::ExecuteListViewSearchOnAllRows(const FString& SearchString,
	const bool bShouldRefreshAfterward)
{
	MainPanelModel.Pin()->GetEditorListModel().Pin()->ExecuteListViewSearchOnAllRows(GetSearchStringFromSearchInputField());
}

EObjectMixerTreeViewMode SObjectMixerEditorMainPanel::GetTreeViewMode()
{
	const TSharedPtr<FObjectMixerEditorMainPanel> PinnedPanelModel = MainPanelModel.Pin();
	check(PinnedPanelModel);
	
	return PinnedPanelModel->GetTreeViewMode();
}

void SObjectMixerEditorMainPanel::SetTreeViewMode(EObjectMixerTreeViewMode InViewMode)
{
	if (TSharedPtr<FObjectMixerEditorMainPanel> PinnedPanelModel = MainPanelModel.Pin())
	{
		PinnedPanelModel->SetTreeViewMode(InViewMode);
	}
}

void SObjectMixerEditorMainPanel::ToggleFilterActive(const FString& FilterName)
{
	if (const TSharedRef<IObjectMixerEditorListFilter>* Match =
		Algo::FindByPredicate(GetShowFilters(),
		[&FilterName](TSharedRef<IObjectMixerEditorListFilter> Comparator)
		{
			return Comparator->GetFilterName().Equals(FilterName);
		}))
	{
		const TSharedRef<IObjectMixerEditorListFilter> Filter = *Match;
		Filter->ToggleFilterActive();

		MainPanelModel.Pin()->GetEditorListModel().Pin()->EvaluateIfRowsPassFilters();
	}
}

const TSet<FName>& SObjectMixerEditorMainPanel::GetCurrentCategorySelection()
{
	return CurrentCategorySelection;
}

void SObjectMixerEditorMainPanel::RebuildCategorySelector()
{
	check(MainPanelModel.IsValid());
	
	CategorySelectorBox->ClearChildren();
	CategorySelectorBox->SetVisibility(EVisibility::Collapsed);

	TArray<FName> AllCategories = MainPanelModel.Pin()->GetAllCategories().Array();
	
	if (AllCategories.IsEmpty())
	{
		// we've selected something that has no sections - rather than show just "All", hide the box
		ResetCurrentCategorySelection();
		return;
	}

	auto CreateSection = [this](FName CategoryName) 
	{
		return SNew(SBox)
			.Padding(FMargin(0))
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "DetailsView.SectionButton")
				.OnCheckStateChanged(this, &SObjectMixerEditorMainPanel::OnCategoryCheckedChanged, CategoryName)
				.IsChecked(this, &SObjectMixerEditorMainPanel::IsCategoryChecked, CategoryName)
				[
					SNew(STextBlock)
					.TextStyle(FAppStyle::Get(), "SmallText")
					.Text(FText::FromName(CategoryName))
				]
			];
	};
	
	AllCategories.Sort([](FName A, FName B)
		{
			return A.LexicalLess(B);
		});

	for (const FName& Key : AllCategories)
	{
		CategorySelectorBox->AddSlot()
		[
			CreateSection(Key)
		];
	}

	CategorySelectorBox->AddSlot()
	[
		CreateSection("All")
	];

	CategorySelectorBox->SetVisibility(EVisibility::Visible);
}

void SObjectMixerEditorMainPanel::OnCategoryCheckedChanged(ECheckBoxState State, FName SectionName)
{
	check(MainPanelModel.IsValid());

	// Remove category
	if (FSlateApplication::Get().GetModifierKeys().IsAltDown())
	{
		MainPanelModel.Pin()->RemoveCategory(SectionName);

		CurrentCategorySelection.Remove(SectionName);
	}
	else
	{
		const bool bIsControlDown = FSlateApplication::Get().GetModifierKeys().IsControlDown();

		if (State == ECheckBoxState::Unchecked)
		{
			if (bIsControlDown)
			{
				CurrentCategorySelection.Remove(SectionName);
			}
			else
			{
				CurrentCategorySelection.Reset();

				if (SectionName != "All")
				{
					CurrentCategorySelection.Add(SectionName);
				}
			}
		}
		else if (State == ECheckBoxState::Checked)
		{
			if (!bIsControlDown)
			{
				CurrentCategorySelection.Reset();
			}

			if (SectionName != "All")
			{
				CurrentCategorySelection.Add(SectionName);
			}
		}
	}

	MainPanelModel.Pin()->GetEditorListModel().Pin()->EvaluateIfRowsPassFilters();
}

ECheckBoxState SObjectMixerEditorMainPanel::IsCategoryChecked(FName Section) const
{
	if (CurrentCategorySelection.IsEmpty() && Section == "All")
	{
		return ECheckBoxState::Checked;
	}

	return CurrentCategorySelection.Contains(Section) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

SObjectMixerEditorMainPanel::~SObjectMixerEditorMainPanel()
{
	if (MainPanelModel.IsValid())
	{
		MainPanelModel.Pin()->GetOnObjectMixerCategoryMapChanged().RemoveAll(this);
		MainPanelModel.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
