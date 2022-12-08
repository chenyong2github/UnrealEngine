// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/MainPanel/SObjectMixerEditorMainPanel.h"

#include "ContentBrowserModule.h"
#include "Engine/Blueprint.h"
#include "ObjectMixerEditorLog.h"
#include "ObjectMixerEditorModule.h"
#include "ObjectMixerEditorSettings.h"
#include "Views/List/ObjectMixerEditorList.h"
#include "Views/List/ObjectMixerEditorListFilters/ObjectMixerEditorListFilter_Collection.h"
#include "Views/MainPanel/ObjectMixerEditorMainPanel.h"
#include "Views/Widgets/SCollectionSelectionButton.h"
#include "Views/Widgets/SObjectMixerPlacementAssetMenuEntry.h"

#include "Algo/RemoveIf.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "EditorActorFolders.h"
#include "IContentBrowserSingleton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IPlacementModeModule.h"
#include "Kismet2/SClassPickerDialog.h"
#include "SPositiveActionButton.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
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
			SAssignNew(CollectionSelectorBox, SWrapBox)
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

	if (UObjectMixerEditorSerializedData* SerializedData = GetMutableDefault<UObjectMixerEditorSerializedData>())
	{
		SerializedData->OnObjectMixerCollectionMapChanged.AddRaw(this, &SObjectMixerEditorMainPanel::RebuildCollectionSelector);
	}

	// Make "All" filter which must always be present
	const TSharedRef<FObjectMixerEditorListFilter_Collection> NewCollectionFilter =
		MakeShared<FObjectMixerEditorListFilter_Collection>(UObjectMixerEditorSerializedData::AllCollectionName);
		
	ListFilters.Add(NewCollectionFilter);
	
	RebuildCollectionSelector();

	SetSingleCollectionSelection();
}

FReply SObjectMixerEditorMainPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Check to see if any actions can be processed
	// If we are in debug mode do not process commands
	if (FSlateApplication::Get().IsNormalExecution())
	{
		if (GetMainPanelModel().Pin()->ObjectMixerElementEditCommands->ProcessCommandBindings(InKeyEvent))
		{
			return FReply::Handled();
		}
	}
	
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

TSharedRef<SWidget> SObjectMixerEditorMainPanel::GenerateToolbar()
{
	TSharedRef<SHorizontalBox> ToolbarBox = SNew(SHorizontalBox);

	// Add object button
	ToolbarBox->AddSlot()
   .HAlign(HAlign_Left)
   .VAlign(VAlign_Center)
   .AutoWidth()
   .Padding(FMargin(8, 4))
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
		.IsChecked_Lambda([]()
		{
			if (const UObjectMixerEditorSettings* Settings = GetDefault<UObjectMixerEditorSettings>())
			{
				return Settings->bSyncSelection ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}
			
			return ECheckBoxState::Undetermined; 
		})
		.OnCheckStateChanged_Lambda([](ECheckBoxState InNewState)
		{
			if (UObjectMixerEditorSettings* Settings = GetMutableDefault<UObjectMixerEditorSettings>())
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

	// Create Folder
	ToolbarBox->AddSlot()
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Center)
	.AutoWidth()
	.Padding(8.f, 1.f, 0.f, 1.f)
	[
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ToolTipText_Lambda([this] ()
		{
			return CanCreateFolder() ? LOCTEXT("CreateFolderToolTip", "Create a new folder containing the current selection") :
				LOCTEXT("CannotCreateFolder","Please select an item in the tree view in order to create a folder.");
		})
		.OnClicked_Lambda([this] ()
		{
			if (CanCreateFolder())
			{
				if (const TSharedPtr<FObjectMixerEditorList> PinnedList = GetMainPanelModel().Pin()->GetEditorListModel().Pin())
				{
					PinnedList->OnRequestNewFolder();

					return FReply::Handled();
				}
			}

			return FReply::Unhandled();
		})
		.IsEnabled(this, &SObjectMixerEditorMainPanel::CanCreateFolder)
		[
			SNew(SImage)
			.ColorAndOpacity(FSlateColor::UseForeground())
			.Image(FAppStyle::Get().GetBrush("SceneOutliner.NewFolderIcon"))
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

bool SObjectMixerEditorMainPanel::CanCreateFolder() const
{
	if (TSharedPtr<FObjectMixerEditorList> PinnedList = GetMainPanelModel().Pin()->GetEditorListModel().Pin())
	{
		return PinnedList->GetSelectedTreeViewItemCount() > 0;
	}

	return false;
}

TSharedRef<SWidget> SObjectMixerEditorMainPanel::OnGenerateAddObjectButtonMenu() const
{
	TSet<UClass*> ClassesToPlace;
	for (const TObjectPtr<UObjectMixerObjectFilter>& Instance : MainPanelModel.Pin()->GetObjectFilterInstances())
	{
		const TSet<TSubclassOf<AActor>> SubclassesOfActor = Instance->GetObjectClassesToPlace();
		if (SubclassesOfActor.Num() > 0)
		{
			ClassesToPlace.Append(
				Instance->GetParentAndChildClassesFromSpecifiedClasses(
					SubclassesOfActor,
					Instance->GetObjectMixerPlacementClassInclusionOptions()
				)
			);
		}
	}

	if (ClassesToPlace.Num() > 0)
	{
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

bool IsBlueprintFilter(const FAssetData& BlueprintClassData)
{
	UClass* BlueprintFilterClass = UObjectMixerBlueprintObjectFilter::StaticClass();
		
	const FString NativeParentClassPath = BlueprintClassData.GetTagValueRef<FString>(FBlueprintTags::NativeParentClassPath);
	const FSoftClassPath ClassPath(NativeParentClassPath);
			
	UClass* NativeParentClass = ClassPath.ResolveClass();
	const bool bInheritsFromBlueprintFilter =
		NativeParentClass // Class may have been removed, or renamed and not correctly redirected
		&& (NativeParentClass == BlueprintFilterClass || NativeParentClass->IsChildOf(BlueprintFilterClass));

	return bInheritsFromBlueprintFilter;
}

struct FAssetClassMap
{
	TObjectPtr<UClass> Class = nullptr;
	FAssetData AssetData;

	bool operator==(const FAssetClassMap& Other) const
	{
		return Class == Other.Class;
	}
};

TSharedRef<SWidget> SObjectMixerEditorMainPanel::OnGenerateFilterClassMenu()
{
	TSharedRef<SVerticalBox> VerticalBox = SNew(SVerticalBox);

	TSharedRef<SBox> OuterBox =
		SNew(SBox)
		.Padding(8)
		[
			VerticalBox
		];

	// Get C++ Derivatives (and maybe Blueprint derivatives)
	TArray<UClass*> DerivedClasses;
	GetDerivedClasses(UObjectMixerObjectFilter::StaticClass(), DerivedClasses, true);

	DerivedClasses.Remove(UObjectMixerObjectFilter::StaticClass());
	DerivedClasses.Remove(UObjectMixerBlueprintObjectFilter::StaticClass());

	TArray<FAssetClassMap> AssetClassMaps;

	for (UClass* Class : DerivedClasses)
	{
		AssetClassMaps.Add({Class});
	}

	// Get remaining Blueprint derivatives
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked< FAssetRegistryModule >(FName("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray< FAssetData > Assets;
	AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), Assets);	
	for (const FAssetData& Asset : Assets)
	{
		if (IsBlueprintFilter(Asset))
		{
			if (const UBlueprint* BlueprintAsset = Cast<UBlueprint>(Asset.GetAsset()))
			{
				UClass* LoadedClass = BlueprintAsset->GeneratedClass;
				if (ensure(LoadedClass && BlueprintAsset->ParentClass))
				{
					if (FAssetClassMap* Match = Algo::FindByPredicate(
						AssetClassMaps,
						[LoadedClass](const FAssetClassMap& ClassMap)
						{
							return ClassMap.Class == LoadedClass;
						}))
					{
						Match->AssetData = Asset;
					}
					else
					{
						AssetClassMaps.Add({LoadedClass, Asset});
					}
				}
			}
		}
	}

	if (AssetClassMaps.Num())
	{
		check(MainPanelModel.Pin());

		AssetClassMaps.Sort([](const FAssetClassMap& A, const FAssetClassMap& B)
		{
			return A.Class.GetFName().LexicalLess(B.Class.GetFName());
		});
		
		FilterClassSelectionInfos.Empty(FilterClassSelectionInfos.Num());
		for (const FAssetClassMap& AssetClassMap : AssetClassMaps)
		{
			if (IsValid(AssetClassMap.Class))
			{
				if (AssetClassMap.Class->GetName().StartsWith(TEXT("SKEL_")) || AssetClassMap.Class->GetName().StartsWith(TEXT("REINST_")))
				{
					continue;
				}

				if (AssetClassMap.Class->HasAnyClassFlags(CLASS_Abstract | CLASS_HideDropDown | CLASS_Deprecated))
				{
					continue;
				}

				const bool bIsDefaultClass =
					// If this is a made-to-purpose sub-plugin of Object Mixer, don't allow default class to be disabled
					GetMainPanelModel().Pin()->GetModuleName() != FObjectMixerEditorModule::BaseObjectMixerModuleName &&
					MainPanelModel.Pin()->GetDefaultFilterClass() == AssetClassMap.Class;
				
				const FText TooltipText = bIsDefaultClass ?
					FText::Format(
						LOCTEXT("DefaultClassDisclaimer","This class explicitly cannot be disabled in {0}"), 
						FText::FromName(MainPanelModel.Pin()->GetModuleName())) :
					FText::FromString(AssetClassMap.Class->GetClassPathName().ToString()
				);

				FilterClassSelectionInfos.Add({AssetClassMap.Class, MainPanelModel.Pin()->IsClassSelected(AssetClassMap.Class)});

				VerticalBox->AddSlot()
				.Padding(FMargin(0, 0, 0, 8))
				.AutoHeight()
				[
					SNew(SFilterClassMenuItem, AssetClassMap, bIsDefaultClass, FilterClassSelectionInfos, TooltipText)
				];
			}
		}

		VerticalBox->AddSlot()
		.HAlign(HAlign_Fill)
		[
			SNew(SButton)
			.Text(LOCTEXT("SelectFilterClassMenu_ApplyButton", "Apply"))
			.HAlign(HAlign_Center)
			.OnClicked(FOnClicked::CreateLambda([this]()
			   {
					if (const TSharedPtr<FObjectMixerEditorMainPanel> PinnedMainPanel = MainPanelModel.Pin())
					{
						PinnedMainPanel->ResetObjectFilterClasses(false);
						for (const FFilterClassSelectionInfo& Info : FilterClassSelectionInfos)
						{
							if (Info.bIsUserSelected)
							{
								PinnedMainPanel->AddObjectFilterClass(Info.Class, false);
							}
						}

						PinnedMainPanel->CacheAndRebuildFilters();
					}

					return FReply::Handled();
			   })
			)
		];
	}
	else
	{
		VerticalBox->AddSlot()
		.HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoFilterClassesAvailable", "No filter classes available."))
		];
	}

	FChildren* ChildWidgets = VerticalBox->GetChildren();
	for (int32 ChildItr = 0; ChildItr < ChildWidgets->Num(); ChildItr++)
	{
		const TSharedRef<SWidget>& Child = ChildWidgets->GetChildAt(ChildItr);

		Child->EnableToolTipForceField(false);
	}
	VerticalBox->EnableToolTipForceField(false);
	OuterBox->EnableToolTipForceField(false);
	
	return OuterBox;
}

TSharedRef<SWidget> SObjectMixerEditorMainPanel::BuildShowOptionsMenu()
{
	FMenuBuilder ShowOptionsMenuBuilder = FMenuBuilder(true, nullptr);

	// No need to select filter class from outside generic instance
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
			LOCTEXT("ClearSoloStatesMenuOption","Clear Solo States"), 
			LOCTEXT("ClearSoloStatesMenuOptionTooltip","Remove the solo state from all rows in this list."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda(
				[this]()
				{
					if (const TSharedPtr<FObjectMixerEditorList> ListModel = MainPanelModel.Pin()->GetEditorListModel().Pin())
					{
						ListModel->ClearSoloRows();
						ListModel->EvaluateAndSetEditorVisibilityPerRow();
					}
				})
			)
		);

		ShowOptionsMenuBuilder.AddMenuEntry(
			LOCTEXT("RebuildListMenuOption","Rebuild List"), 
			LOCTEXT("RebuildListMenuOptionTooltip","Force the active list to be rebuilt. Useful if the list doesn't automatically refresh."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(MainPanelModel.Pin().ToSharedRef(), &FObjectMixerEditorMainPanel::RequestRebuildList)));
	}
	ShowOptionsMenuBuilder.EndSection();

	if (ListFilters.Num())
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

			for (const TSharedRef<IObjectMixerEditorListFilter>& Filter : ListFilters)
			{
				// Don't add non-toggle filters to Show Options, they're always on
				if (Filter->IsUserToggleable())
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

void SObjectMixerEditorMainPanel::ResetCollectionFilters()
{
	// Clean up all collection filters
	for (int32 FiltersItr = ListFilters.Num() - 1; FiltersItr >= 0; FiltersItr--)
	{
		if (const TSharedPtr<FObjectMixerEditorListFilter_Collection> CollectionFilter =
			StaticCastSharedRef<FObjectMixerEditorListFilter_Collection>(ListFilters[FiltersItr]))
		{
			ListFilters.RemoveAt(FiltersItr);
		}
	}
}

void SObjectMixerEditorMainPanel::SetSingleCollectionSelection(const FName& CollectionToEnableName)
{
	// Disable all collection filters except CollectionToEnableName
	for (const TSharedRef<IObjectMixerEditorListFilter>& Filter : GetListFilters())
	{
		if (const TSharedPtr<FObjectMixerEditorListFilter_Collection> CollectionFilter =
			StaticCastSharedRef<FObjectMixerEditorListFilter_Collection>(Filter))
		{
			const bool bMatch =
				CollectionFilter->CollectionName.IsEqual(CollectionToEnableName);
			CollectionFilter->SetFilterActive(bMatch);
		}
	}
}

void SFilterClassMenuItem::Construct(const FArguments& InArgs, FAssetClassMap AssetClassMap, const bool bIsDefaultClass,
	TArray<SObjectMixerEditorMainPanel::FFilterClassSelectionInfo>& FilterClassSelectionInfos, const FText TooltipText)
{
	const bool bHasValidAssetData = AssetClassMap.AssetData.IsValid();
	ChildSlot
	[
		SNew(SHorizontalBox)
		.ToolTipText(TooltipText)
		.IsEnabled(!bIsDefaultClass)

		+SHorizontalBox::Slot()
		.Padding(FMargin(0, 0, 8, 0))
		.AutoWidth()
		[
			SNew(SCheckBox)
			.OnCheckStateChanged_Lambda([this, AssetClassMap, &FilterClassSelectionInfos](ECheckBoxState NewState)
		   {
				if (SObjectMixerEditorMainPanel::FFilterClassSelectionInfo* Match =
					Algo::FindByPredicate(FilterClassSelectionInfos,
						[AssetClassMap](const SObjectMixerEditorMainPanel::FFilterClassSelectionInfo& Other)
						{
							return Other.Class == AssetClassMap.Class;
						}))
				{
					   Match->bIsUserSelected = !Match->bIsUserSelected;
				}
		   })
		   .IsChecked_Lambda([this, AssetClassMap, &FilterClassSelectionInfos]()
		   {
		   		bool bShouldBeChecked = false;
		   	
				if (const SObjectMixerEditorMainPanel::FFilterClassSelectionInfo* Match =
			   		Algo::FindByPredicate(FilterClassSelectionInfos,
						[AssetClassMap](const SObjectMixerEditorMainPanel::FFilterClassSelectionInfo& Other)
						{
							return Other.Class == AssetClassMap.Class;
						}))
			   {
				   bShouldBeChecked = Match->bIsUserSelected;
			   }
			   return bShouldBeChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		   })
		]

		+SHorizontalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		.Padding(FMargin(0, 0, 8, 0))
		[
			SNew(STextBlock)
			.Text(FText::FromString(
				AssetClassMap.Class->GetName().EndsWith(TEXT("_C")) ? AssetClassMap.Class->GetName().LeftChop(2) : AssetClassMap.Class->GetName()))
		]

		+SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.AutoWidth()	
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ContentPadding(FMargin(2))
			.IsEnabled(bHasValidAssetData)
			.ToolTipText(bHasValidAssetData ?
				LOCTEXT("BrowseTooltip", "Browses to the associated asset and selects it in the most recently used Content Browser (summoning one if necessary)")
					: LOCTEXT("NoBlueprintFilterFound", "This filter class is not a Blueprint class."))
			.OnClicked_Lambda([AssetClassMap]()
			{
				const FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
				ContentBrowserModule.Get().SyncBrowserToAssets({AssetClassMap.AssetData});

				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("SystemWideCommands.FindInContentBrowser.Small"))
			]
		]
	];
}

FText SObjectMixerEditorMainPanel::GetSearchTextFromSearchInputField() const
{
	return ensureAlwaysMsgf(SearchBoxPtr.IsValid(),
		TEXT("%hs: SearchBoxPtr is not valid. Check to make sure it was created."), __FUNCTION__)
	? SearchBoxPtr->GetText() : FText::GetEmpty();
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
		Algo::FindByPredicate(GetListFilters(),
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

TArray<TWeakPtr<IObjectMixerEditorListFilter>> SObjectMixerEditorMainPanel::GetWeakActiveListFiltersSortedByName()
{
	TArray<TWeakPtr<IObjectMixerEditorListFilter>> ActiveFilters = {};
	
	for (const TSharedRef<IObjectMixerEditorListFilter>& ListFilter : GetListFilters())
	{
		if (ListFilter->GetIsFilterActive())
		{
			ActiveFilters.Add(ListFilter);
		}
	}

	ActiveFilters.StableSort(
		[](const TWeakPtr<IObjectMixerEditorListFilter>& A, const TWeakPtr<IObjectMixerEditorListFilter>& B)
		{
			FString NameA = A.IsValid() ? A.Pin()->GetFilterName() : "";
			FString NameB = A.IsValid() ? A.Pin()->GetFilterName() : "";
			return NameA < NameB;
		});

	return ActiveFilters;
}

TSet<TSharedRef<FObjectMixerEditorListFilter_Collection>> SObjectMixerEditorMainPanel::GetCurrentCollectionSelection()
{
	TSet<TSharedRef<FObjectMixerEditorListFilter_Collection>> CollectionFilters;
	for (const TSharedRef<IObjectMixerEditorListFilter>& ListFilter : GetListFilters())
	{
		if (const TSharedPtr<FObjectMixerEditorListFilter_Collection> CollectionFilter =
			StaticCastSharedRef<FObjectMixerEditorListFilter_Collection>(ListFilter))
		{
			CollectionFilters.Add(CollectionFilter.ToSharedRef());
		}
	}
	
	return CollectionFilters;
}

void SObjectMixerEditorMainPanel::RebuildCollectionSelector()
{
	check(MainPanelModel.IsValid());

	// Make user collections

	CollectionSelectorBox->ClearChildren();
	CollectionSelectorBox->SetVisibility(EVisibility::Collapsed);

	auto CreateCollectionFilterAndAddToCollectionSelector =
		[this](const TSharedRef<FObjectMixerEditorListFilter_Collection> NewCollectionFilter) 
	{
		CollectionSelectorBox->AddSlot()
		[
			SNew(SCollectionSelectionButton, SharedThis(this), NewCollectionFilter)
		];
	};

	TArray<FName> AllCollectionNames = MainPanelModel.Pin()->GetAllCollectionNames();
	
	// Remove any collection filters that no longer match (except "All")
	ListFilters.SetNum(Algo::StableRemoveIf(ListFilters,
		[&AllCollectionNames](const TSharedRef<IObjectMixerEditorListFilter>& ListFilter)
		{
			if (const TSharedPtr<FObjectMixerEditorListFilter_Collection> CollectionFilter =
				StaticCastSharedRef<FObjectMixerEditorListFilter_Collection>(ListFilter))
			{
				const bool bIsAllCollection = CollectionFilter->CollectionName.IsEqual(UObjectMixerEditorSerializedData::AllCollectionName);
				const bool bIsFilterNameFoundInAllCollectionNames = AllCollectionNames.Contains(CollectionFilter->CollectionName);
				
				return !bIsAllCollection && !bIsFilterNameFoundInAllCollectionNames;
			}
			return false;
		})
	);

	const TSharedRef<IObjectMixerEditorListFilter>* AllCollection =
		Algo::FindByPredicate(
			ListFilters,
			[&AllCollectionNames](const TSharedRef<IObjectMixerEditorListFilter>& ListFilter)
			{
				if (const TSharedPtr<FObjectMixerEditorListFilter_Collection> CollectionFilter =
					StaticCastSharedRef<FObjectMixerEditorListFilter_Collection>(ListFilter))
				{
					return CollectionFilter->CollectionName.IsEqual(UObjectMixerEditorSerializedData::AllCollectionName);
				}
				return false;
			});
	
	// No collections - rather than show "All", just keep the box hidden
	if (AllCollectionNames.IsEmpty())
	{
		// Set "All" filter to active
		if (AllCollection)
		{
			(*AllCollection)->SetFilterActive(true);
		}
		
		return;
	}

	// Make "All" collection widget
	if (AllCollection)
	{
		CreateCollectionFilterAndAddToCollectionSelector(
			StaticCastSharedRef<FObjectMixerEditorListFilter_Collection>(*AllCollection));
	}
	
	TMap<FName, TSharedRef<FObjectMixerEditorListFilter_Collection>> CollectionNamesToFilters; 
	for (const FName& Key : AllCollectionNames)
	{
		// Try to find a matching filter
		if (const TSharedRef<IObjectMixerEditorListFilter>* Match = Algo::FindByPredicate(
			ListFilters,
			[&Key](const TSharedRef<IObjectMixerEditorListFilter>& ListFilter)
			{
				if (const TSharedPtr<FObjectMixerEditorListFilter_Collection> CollectionFilter =
					StaticCastSharedRef<FObjectMixerEditorListFilter_Collection>(ListFilter))
				{
					return CollectionFilter->CollectionName.IsEqual(Key);
				}
				return false;
			}))
		{
			CollectionNamesToFilters.Add(Key, StaticCastSharedRef<FObjectMixerEditorListFilter_Collection>(*Match));
		}
		else
		{
			// Otherwise create new filter for unmatched collection name
			TSharedRef<FObjectMixerEditorListFilter_Collection> NewCollectionFilter =
				MakeShared<FObjectMixerEditorListFilter_Collection>(Key);
			ListFilters.Add(NewCollectionFilter);
			CollectionNamesToFilters.Add(Key, NewCollectionFilter);
		}

		// Then create widgets for each key
		if (const TSharedRef<FObjectMixerEditorListFilter_Collection>* FoundCollectionFilter = CollectionNamesToFilters.Find(Key))
		{
			CreateCollectionFilterAndAddToCollectionSelector(*FoundCollectionFilter);
		}
	}

	CollectionSelectorBox->SetVisibility(EVisibility::Visible);
}

bool SObjectMixerEditorMainPanel::RequestRemoveCollection(const FName& CollectionName)
{
	if (MainPanelModel.Pin()->RequestRemoveCollection(CollectionName))
	{
		const int32 NewListFilterCount = Algo::StableRemoveIf(ListFilters,
			[CollectionName](const TSharedRef<IObjectMixerEditorListFilter>& ListFilter)
			{
				if (const TSharedPtr<FObjectMixerEditorListFilter_Collection> CollectionFilter =
					StaticCastSharedRef<FObjectMixerEditorListFilter_Collection>(ListFilter))
				{
					return CollectionFilter->CollectionName.IsEqual(CollectionName);
				}
				return false;
			});
		
		ListFilters.SetNum(NewListFilterCount);

		OnCollectionCheckedStateChanged(true, UObjectMixerEditorSerializedData::AllCollectionName);

		return true;
	}

	return false;
}

bool SObjectMixerEditorMainPanel::RequestDuplicateCollection(const FName& CollectionToDuplicateName, FName& DesiredDuplicateName) const
{
	return MainPanelModel.Pin()->RequestDuplicateCollection(CollectionToDuplicateName, DesiredDuplicateName);
}

bool SObjectMixerEditorMainPanel::RequestRenameCollection(
	const FName& CollectionNameToRename,
	const FName& NewCollectionName)
{
	return MainPanelModel.Pin()->RequestRenameCollection(CollectionNameToRename, NewCollectionName);
}

bool SObjectMixerEditorMainPanel::DoesCollectionExist(const FName& CollectionName) const
{
	return MainPanelModel.Pin()->DoesCollectionExist(CollectionName);
}

void SObjectMixerEditorMainPanel::OnCollectionCheckedStateChanged(bool bShouldBeChecked, FName CollectionName)
{
	check(MainPanelModel.IsValid());

	GetMainPanelModel().Pin()->OnPreFilterChange.Broadcast();

	const bool bIsAllCollection = CollectionName.IsEqual(UObjectMixerEditorSerializedData::AllCollectionName);

	const bool bIsControlDown = FSlateApplication::Get().GetModifierKeys().IsControlDown();

	if (bShouldBeChecked)
	{
		if (bIsControlDown)
		{
			if (bIsAllCollection)
			{
				// Can't multi-select All
				return;
			}
			
			// Enable this collection and disable "All"
			for (const TSharedRef<IObjectMixerEditorListFilter>& Filter : GetListFilters())
			{
				if (const TSharedPtr<FObjectMixerEditorListFilter_Collection> CollectionFilter =
					StaticCastSharedRef<FObjectMixerEditorListFilter_Collection>(Filter))
				{
					if (CollectionFilter->CollectionName.IsEqual(CollectionName))
					{
						CollectionFilter->SetFilterActive(true);
					}
					else if (CollectionFilter->CollectionName.IsEqual(UObjectMixerEditorSerializedData::AllCollectionName))
					{
						CollectionFilter->SetFilterActive(false);
					}
				}
			}
		}
		else
		{
			SetSingleCollectionSelection(CollectionName);
		}
	}
	else
	{		
		if (bIsControlDown)
		{
			if (bIsAllCollection)
			{
				// Can't disable All
				return;
			}
			
			// Disable just this collection
			int32 ActiveFilterCount = 0;
			for (const TSharedRef<IObjectMixerEditorListFilter>& Filter : GetListFilters())
			{
				if (const TSharedPtr<FObjectMixerEditorListFilter_Collection> CollectionFilter =
					StaticCastSharedRef<FObjectMixerEditorListFilter_Collection>(Filter))
				{
					if (CollectionFilter->CollectionName.IsEqual(CollectionName))
					{
						CollectionFilter->SetFilterActive(false);
					}
					else if (CollectionFilter->GetIsFilterActive())
					{
						ActiveFilterCount++;
					}
				}
			}

			if (ActiveFilterCount == 0)
			{
				// Reset to all
				SetSingleCollectionSelection();
			}
		}
		else
		{
			if (bIsAllCollection)
			{
				// Reset to all
				SetSingleCollectionSelection();
			}
			
			// Set just this filter active
			SetSingleCollectionSelection(CollectionName);
		}
	}

	GetMainPanelModel().Pin()->OnPostFilterChange.Broadcast();
}

ECheckBoxState SObjectMixerEditorMainPanel::IsCollectionChecked(FName CollectionName) const
{
	const TSharedRef<IObjectMixerEditorListFilter>* Match = Algo::FindByPredicate(ListFilters,
		[CollectionName](const TSharedRef<IObjectMixerEditorListFilter>& ListFilter)
		{
			if (ListFilter->GetIsFilterActive())
			{
				if (const TSharedPtr<FObjectMixerEditorListFilter_Collection> CollectionFilter =
				   StaticCastSharedRef<FObjectMixerEditorListFilter_Collection>(ListFilter))
				{
					return CollectionFilter->CollectionName.IsEqual(CollectionName);
				}
			}
			return false;
		});

	return Match ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

SObjectMixerEditorMainPanel::~SObjectMixerEditorMainPanel()
{
	MainPanelModel.Reset();
}

#undef LOCTEXT_NAMESPACE
