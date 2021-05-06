// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraAssetPickerList.h"
#include "NiagaraEmitter.h"
#include "NiagaraEditorStyle.h"

#include "AssetData.h"
#include "AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "AssetThumbnail.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "EditorStyleSet.h"
#include "NiagaraEditorUtilities.h"

#define LOCTEXT_NAMESPACE "SNiagaraAssetSelector"

FText SNiagaraAssetPickerList::NiagaraPluginCategory = LOCTEXT("NiagaraCategory", "Engine (Niagara Plugin)");
FText SNiagaraAssetPickerList::ProjectCategory = LOCTEXT("ProjectCategory", "Project");
FText SNiagaraAssetPickerList::LibraryCategory = LOCTEXT("Library", "Library");
FText SNiagaraAssetPickerList::NonLibraryCategory = LOCTEXT("NotInLibrary", "Not in Library");
FText SNiagaraAssetPickerList::UncategorizedCategory = LOCTEXT("Uncategorized", "Uncategorized");
ENiagaraScriptTemplateSpecification SNiagaraAssetPickerList::CachedActiveTab = ENiagaraScriptTemplateSpecification::Template;

bool FNiagaraAssetPickerTabOptions::IsTabAvailable(ENiagaraScriptTemplateSpecification AssetTab) const
{
	if(TabData.Contains(AssetTab))
	{
		return TabData[AssetTab];
	}

	return false;
}

int32 FNiagaraAssetPickerTabOptions::GetNumAvailableTabs() const
{
	TArray<bool> StateArray;
	TabData.GenerateValueArray(StateArray);

	int32 NumAvailableTabs = 0;
	for(const bool& State : StateArray)
	{
		if(State)
		{
			NumAvailableTabs++;
		}
	}

	return NumAvailableTabs;
}

bool FNiagaraAssetPickerTabOptions::GetOnlyAvailableTab(ENiagaraScriptTemplateSpecification& OutTab) const
{
	int32 ActiveCount = 0;
	ENiagaraScriptTemplateSpecification Tab = ENiagaraScriptTemplateSpecification::None;
	for(const auto& TabEntry : TabData)
	{
		if(TabEntry.Value == true)
		{
			ActiveCount++;
			Tab = TabEntry.Key;
		}
	}

	if(ActiveCount == 1)
	{
		OutTab = Tab;
		return true;
	}

	return false;
}

bool FNiagaraAssetPickerTabOptions::GetOnlyShowTemplates() const
{
	ENiagaraScriptTemplateSpecification Tab;
	bool bFound = GetOnlyAvailableTab(Tab);
	return bFound && Tab == ENiagaraScriptTemplateSpecification::Template;
}

const TMap<ENiagaraScriptTemplateSpecification, bool>& FNiagaraAssetPickerTabOptions::GetTabData() const
{
	return TabData;
}

void SNiagaraAssetPickerList::Construct(const FArguments& InArgs, UClass* AssetClass)
{
	AssetThumbnailPool = MakeShareable(new FAssetThumbnailPool(24));

	OnTemplateAssetActivated = InArgs._OnTemplateAssetActivated;
	ViewOptions = InArgs._ViewOptions;
	TabOptions = InArgs._TabOptions;
	CustomFilter = InArgs._OnDoesAssetPassCustomFilter;
	bLibraryOnly = InArgs._bLibraryOnly;
	
	SAssignNew(SourceFilterBox, SNiagaraSourceFilterBox)
    .OnFiltersChanged(this, &SNiagaraAssetPickerList::TriggerRefresh);
	
	InitializeTemplateTabs();

	TArray<FAssetData> EmittersToShow = GetAssetDataForSelector(AssetClass);
	
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Right)
		.AutoHeight()
		.Padding(3.f)
		[
			SNew(SCheckBox)
			.OnCheckStateChanged(this, &SNiagaraAssetPickerList::LibraryCheckBoxStateChanged)
			.IsChecked(this, &SNiagaraAssetPickerList::GetLibraryCheckBoxState)
			.Visibility(ViewOptions.GetAddLibraryOnlyCheckbox() ? EVisibility::Visible : EVisibility::Collapsed)			
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("LibraryOnly", "Library Only"))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SourceFilterBox.ToSharedRef()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
        [
            TabBox.ToSharedRef()
        ]
		+ SVerticalBox::Slot()
		[
			SAssignNew(ItemSelector, SNiagaraAssetItemSelector)
			.Items(EmittersToShow)
			.OnGetCategoriesForItem(this, &SNiagaraAssetPickerList::OnGetCategoriesForItem)
			.OnCompareCategoriesForEquality(this, &SNiagaraAssetPickerList::OnCompareCategoriesForEquality)
			.OnCompareCategoriesForSorting(this, &SNiagaraAssetPickerList::OnCompareCategoriesForSorting)
			.OnCompareItemsForSorting(this, &SNiagaraAssetPickerList::OnCompareItemsForSorting)
			.OnDoesItemMatchFilterText(this, &SNiagaraAssetPickerList::OnDoesItemMatchFilterText)
			.OnGenerateWidgetForCategory(this, &SNiagaraAssetPickerList::OnGenerateWidgetForCategory)
			.OnGenerateWidgetForItem(this, &SNiagaraAssetPickerList::OnGenerateWidgetForItem)
			.OnItemActivated(this, &SNiagaraAssetPickerList::OnItemActivated)
			.AllowMultiselect(InArgs._bAllowMultiSelect)
			.OnDoesItemPassCustomFilter(this, &SNiagaraAssetPickerList::DoesItemPassCustomFilter)
			.RefreshItemSelectorDelegates(InArgs._RefreshItemSelectorDelegates)
			.ClickActivateMode(InArgs._ClickActivateMode)
			.ExpandInitially(true)
		]
	];

	FSlateApplication::Get().SetKeyboardFocus(ItemSelector->GetSearchBox());
}

void SNiagaraAssetPickerList::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	AssetThumbnailPool->Tick(InDeltaTime);
}

TArray<FAssetData> SNiagaraAssetPickerList::GetSelectedAssets() const
{
	return ItemSelector->GetSelectedItems();
}

void SNiagaraAssetPickerList::RefreshAll() const
{
	ItemSelector->RefreshAllCurrentItems();
}

void SNiagaraAssetPickerList::ExpandTree()
{
	ItemSelector->ExpandTree();
}

TSharedRef<SWidget> SNiagaraAssetPickerList::GetSearchBox() const
{
	return ItemSelector->GetSearchBox();
}

void SNiagaraAssetPickerList::InitializeTemplateTabs()
{
	SAssignNew(TabBox, SHorizontalBox);

	int32 NumTabs = TabOptions.GetNumAvailableTabs();
	bool bActiveTabSet = false;

	// we restore 
	if(TabOptions.IsTabAvailable(CachedActiveTab))
	{
		ActiveTab = CachedActiveTab;
		bUseActiveTab = true;
		bActiveTabSet = true;
	}
	
	// if we have one tab available, we want to activate it for the item filter to work
	if(NumTabs == 1)
	{
		TabOptions.GetOnlyAvailableTab(ActiveTab);
		bUseActiveTab = true;
	}
	// we only want to add tab widgets if we have more than one tab
	else if(NumTabs > 1)
	{
		if(TabOptions.IsTabAvailable(ENiagaraScriptTemplateSpecification::Template))
		{
			// we set the currently active tab using the first available tab in a set order
			if(bActiveTabSet == false)
			{
				ActiveTab = ENiagaraScriptTemplateSpecification::Template;
				bUseActiveTab = true;
				bActiveTabSet = true;
			}

			TabBox->AddSlot()
			.Padding(5.f)
			[
				SNew(SBorder)
				.ToolTipText(LOCTEXT("TemplateTabTooltip", "Templates are intended as starting points for building functional emitters of different types,\n"
					"and are copied into a system as a unique emitter with no inheritance"))
				.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SCheckBox)
					.Style(FNiagaraEditorStyle::Get(), "GraphActionMenu.FilterCheckBox")
					.BorderBackgroundColor(this, &SNiagaraAssetPickerList::GetBackgroundColor, ENiagaraScriptTemplateSpecification::Template)
					.ForegroundColor(this, &SNiagaraAssetPickerList::GetTabForegroundColor, ENiagaraScriptTemplateSpecification::Template)
					.IsChecked_Lambda([&]()
					{
						return ActiveTab == ENiagaraScriptTemplateSpecification::Template ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged(this, &SNiagaraAssetPickerList::OnTabActivated, ENiagaraScriptTemplateSpecification::Template)
				   [
				       SNew(STextBlock)
				       .TextStyle(&FNiagaraEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("GraphActionMenu.TemplateTabTextBlock"))
				       .Justification(ETextJustify::Center)
				       .Text(LOCTEXT("TemplateTabLabel", "Templates"))
				   ]
				]
			];
		}

		if(TabOptions.IsTabAvailable(ENiagaraScriptTemplateSpecification::None))
		{
			if(bActiveTabSet == false)
			{
				ActiveTab = ENiagaraScriptTemplateSpecification::None;
				bUseActiveTab = true;
				bActiveTabSet = true;
			}
			
			TabBox->AddSlot()
			.Padding(5.f)
            [
	            SNew(SBorder)
	            .ToolTipText(LOCTEXT("ParentTabTooltip", "Parent Emitters assets are inherited as children and will receive changes from the parent emitter,\n"
	            	"and are meant to serve as art directed initial behaviors which can be propagated throughout a project quickly and easily.\n"
	            	"Over time, a library of parent emitters can be used to speed up the construction of complex effects specific to your project."))
	            .BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				[
		            SNew(SCheckBox)
					.Style(FNiagaraEditorStyle::Get(), "GraphActionMenu.FilterCheckBox")
					.BorderBackgroundColor(this, &SNiagaraAssetPickerList::GetBackgroundColor, ENiagaraScriptTemplateSpecification::None)
					.ForegroundColor(this, &SNiagaraAssetPickerList::GetTabForegroundColor, ENiagaraScriptTemplateSpecification::None)
					.IsChecked_Lambda([&]()
					{
						return ActiveTab == ENiagaraScriptTemplateSpecification::None ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged(this, &SNiagaraAssetPickerList::OnTabActivated, ENiagaraScriptTemplateSpecification::None)
				     [
				         SNew(STextBlock)
				         .TextStyle(&FNiagaraEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("GraphActionMenu.TemplateTabTextBlock"))
				         .Justification(ETextJustify::Center)
				         .Text(LOCTEXT("ParentTabLabel", "Parents"))
				     ]
				]
            ];
		}

		if(TabOptions.IsTabAvailable(ENiagaraScriptTemplateSpecification::Behavior))
		{
			if(bActiveTabSet == false)
			{
				ActiveTab = ENiagaraScriptTemplateSpecification::Behavior;
				bUseActiveTab = true;
				bActiveTabSet = true;
			}
			
			TabBox->AddSlot()
			.Padding(5.f)
            [
	            SNew(SBorder)
	            .ToolTipText(LOCTEXT("BehaviorTabTooltip", "Behavior Examples are intended to serve as a guide to how Niagara works at a feature level.\n"
	            	"Each example shows a simplified setup used to achieve specific outcomes and are intended as starting points, building blocks, or simply as reference.\n"
	            	"These are copied into a system as a unique emitter with no inheritance"))
	            .BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(SCheckBox)
					.Style(FNiagaraEditorStyle::Get(), "GraphActionMenu.FilterCheckBox")
					.BorderBackgroundColor(this, &SNiagaraAssetPickerList::GetBackgroundColor, ENiagaraScriptTemplateSpecification::Behavior)
					.ForegroundColor(this, &SNiagaraAssetPickerList::GetTabForegroundColor, ENiagaraScriptTemplateSpecification::Behavior)
					.IsChecked_Lambda([&]()
					{
						return ActiveTab == ENiagaraScriptTemplateSpecification::Behavior ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
				    .OnCheckStateChanged(this, &SNiagaraAssetPickerList::OnTabActivated, ENiagaraScriptTemplateSpecification::Behavior)
				    [
				        SNew(STextBlock)
				        .TextStyle(&FNiagaraEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("GraphActionMenu.TemplateTabTextBlock"))
				        .Justification(ETextJustify::Center)
				        .Text(LOCTEXT("BehaviorTabLabel", "Behavior Examples"))
				    ]
				]
            ];
		}

		// we cache the active tab if we have more than 1 tab available so we can activate it for other instances
		CachedActiveTab = ActiveTab;
	}	
}

TArray<FAssetData> SNiagaraAssetPickerList::GetAssetDataForSelector(UClass* AssetClass)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> EmitterAssets;
	AssetRegistryModule.Get().GetAssetsByClass(AssetClass->GetFName(), EmitterAssets);

	TArray<FAssetData> EmittersToShow;
	if (TabOptions.GetOnlyShowTemplates())
	{
		for(FAssetData& EmitterAsset :EmitterAssets)
		{ 
			ENiagaraScriptTemplateSpecification TemplateSpecification;
			bool bFoundTemplateScriptTag = FNiagaraEditorUtilities::GetTemplateSpecificationFromTag(EmitterAsset, TemplateSpecification);
			if (bFoundTemplateScriptTag && TemplateSpecification == ENiagaraScriptTemplateSpecification::Template)
			{
				EmittersToShow.Add(EmitterAsset);
			}
		}
	}
	else
	{
		EmittersToShow = EmitterAssets;
	}

	return EmittersToShow;
}

TArray<FText> SNiagaraAssetPickerList::OnGetCategoriesForItem(const FAssetData& Item)
{
	TArray<FText> Categories;

	auto AddUserDefinedCategory = [&Categories, &Item]() {
		FText UserDefinedCategory;
		bool bFoundCategoryTag = Item.GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, Category), UserDefinedCategory);

		if (bFoundCategoryTag == false)
		{
			if (Item.IsAssetLoaded())
			{
				UNiagaraEmitter* EmitterAsset = Cast<UNiagaraEmitter>(Item.GetAsset());
				if (EmitterAsset != nullptr)
				{
					UserDefinedCategory = EmitterAsset->Category;
				}
			}
		}

		if (UserDefinedCategory.IsEmptyOrWhitespace() == false)
		{
			Categories.Add(UserDefinedCategory);
		}
		else
		{
			Categories.Add(UncategorizedCategory);
		}
	};

	auto AddLibraryCategory = [&Categories, &Item, this]() {
		bool bInLibrary = false;
		bool bFoundLibraryTag = Item.GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, bExposeToLibrary), bInLibrary);

		if (bFoundLibraryTag == false)
		{
			if (Item.IsAssetLoaded())
			{
				UNiagaraEmitter* EmitterAsset = Cast<UNiagaraEmitter>(Item.GetAsset());
				if (EmitterAsset != nullptr)
				{
					bInLibrary = EmitterAsset->bExposeToLibrary;
				}
			}
		}

		if (bFoundLibraryTag && bInLibrary)
		{
			Categories.Add(LibraryCategory);
		}
		else
		{
			Categories.Add(NonLibraryCategory);
		}
	};

	auto AddAssetPathCategory = [&Categories, &Item]() {
		TArray<FString> AssetPathParts;
		Item.ObjectPath.ToString().ParseIntoArray(AssetPathParts, TEXT("/"));
		if (AssetPathParts.Num() > 0)
		{
			if (AssetPathParts[0] == TEXT("Niagara"))
			{
				Categories.Add(LOCTEXT("NiagaraCategory", "Engine (Niagara Plugin)"));
			}
			else if (AssetPathParts[0] == TEXT("Game"))
			{
				Categories.Add(LOCTEXT("ProjectCategory", "Project"));
			}
			else
			{
				Categories.Add(FText::Format(LOCTEXT("OtherPluginFormat", "Plugin - {0}"), FText::FromString(AssetPathParts[0])));
			}
		}
	};


	if (ViewOptions.GetCategorizeAssetsByAssetPath())
	{
		AddAssetPathCategory();
	}

	// if library only is turned off, we might want to add a library category to discern between library items
	if (bLibraryOnly == false && ViewOptions.GetCategorizeLibraryAssets())
	{
		AddLibraryCategory();
	}

	if (ViewOptions.GetCategorizeUserDefinedCategory())
	{
		AddUserDefinedCategory();
	}

	return Categories;
}

bool SNiagaraAssetPickerList::OnCompareCategoriesForEquality(const FText& CategoryA, const FText& CategoryB) const
{
	return CategoryA.CompareTo(CategoryB) == 0;
}

bool SNiagaraAssetPickerList::OnCompareCategoriesForSorting(const FText& CategoryA, const FText& CategoryB) const
{
	int32 CompareResult = CategoryA.CompareTo(CategoryB);
	if (TabOptions.GetOnlyShowTemplates())
	{
		if (CompareResult != 0)
		{
			// Project first
			if (CategoryA.CompareTo(ProjectCategory) == 0)
			{
				return true;
			}
			if (CategoryB.CompareTo(ProjectCategory) == 0)
			{
				return false;
			}

			// Niagara plugin second.
			if (CategoryA.CompareTo(NiagaraPluginCategory) == 0)
			{
				return true;
			}
			if (CategoryB.CompareTo(NiagaraPluginCategory) == 0)
			{
				return false;
			}
		}
	}
	else
	{
		if (CompareResult != 0)
		{
			// Library first.
			if (CategoryA.CompareTo(LibraryCategory) == 0)
			{
				return true;
			}
			if (CategoryB.CompareTo(LibraryCategory) == 0)
			{
				return false;
			}
		}
	}
	// Otherwise just return the actual result.
	return CompareResult < 0;
}

bool SNiagaraAssetPickerList::OnCompareItemsForSorting(const FAssetData& ItemA, const FAssetData& ItemB) const
{
	return ItemA.AssetName.ToString().Compare(ItemB.AssetName.ToString()) < 0;
}

bool SNiagaraAssetPickerList::OnDoesItemMatchFilterText(const FText& FilterText, const FAssetData& Item)
{
	TArray<FString> FilterStrings;
	FilterText.ToString().ParseIntoArrayWS(FilterStrings, TEXT(","));

	FString AssetNameString = Item.AssetName.ToString();
	for (const FString& FilterString : FilterStrings)
	{
		if (!AssetNameString.Contains(FilterString))
		{
			return false;
		}
	}

	return true;
}

TSharedRef<SWidget> SNiagaraAssetPickerList::OnGenerateWidgetForCategory(const FText& Category)
{
	if (Category.EqualTo(LibraryCategory) || Category.EqualTo(NonLibraryCategory))
	{
		return SNew(SBox)
			.Padding(FMargin(5, 5, 5, 3))
			[
				SNew(STextBlock)
				.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.AssetPickerAssetSubcategoryText")
				.Text(Category)
			];
	}
	return SNew(SBox)
		.Padding(FMargin(5, 5, 5, 3))
		[
			SNew(STextBlock)
			.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.AssetPickerAssetCategoryText")
			.Text(Category)
		];
}

const int32 ThumbnailSize = 72;

TSharedRef<SWidget> SNiagaraAssetPickerList::OnGenerateWidgetForItem(const FAssetData& Item)
{
	TSharedRef<FAssetThumbnail> AssetThumbnail = MakeShared<FAssetThumbnail>(Item, ThumbnailSize, ThumbnailSize, AssetThumbnailPool);
	FAssetThumbnailConfig ThumbnailConfig;
	ThumbnailConfig.bAllowFadeIn = false;
	
	auto GenerateWidgetForItem_Generic = [&Item, &ThumbnailConfig, &AssetThumbnail, this]()->TSharedRef<SWidget> {
		FText AssetDescription;
		Item.GetTagValue("TemplateAssetDescription", AssetDescription);

		TSharedRef<SWidget> ThumbnailWidget = AssetThumbnail->MakeThumbnailWidget(ThumbnailConfig);
		
		return 	SNew(SHorizontalBox)
			.ToolTipText(AssetDescription)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(15, 3, 3, 3)
			[
				SNew(SBox)
				.WidthOverride(32.0)
				.HeightOverride(32.0)
				[
					ThumbnailWidget
				]
			]
			+ SHorizontalBox::Slot()
			.Padding(3)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FName::NameToDisplayString(Item.AssetName.ToString(), false)))
				.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.AssetPickerAssetNameText")
				.HighlightText(this, &SNiagaraAssetPickerList::GetFilterText)
			];
	};

	auto GenerateWidgetForItem_ExpandTemplateAndLibraryItems = 
		[&GenerateWidgetForItem_Generic, &Item, &ThumbnailConfig, &AssetThumbnail, this]()->TSharedRef<SWidget> 
	{
		FText AssetDescription;
		Item.GetTagValue("TemplateAssetDescription", AssetDescription);

		bool bIsTemplate = false;
		bool bInLibrary = false;
		ENiagaraScriptTemplateSpecification TemplateSpecification;
		bool bFoundTemplateScriptTag = FNiagaraEditorUtilities::GetTemplateSpecificationFromTag(Item, TemplateSpecification);

		if (bFoundTemplateScriptTag == false)
		{
			if (Item.IsAssetLoaded())
			{
				UNiagaraEmitter* EmitterAsset = Cast<UNiagaraEmitter>(Item.GetAsset());
				if (EmitterAsset != nullptr)
				{
					bIsTemplate = EmitterAsset->TemplateSpecification == ENiagaraScriptTemplateSpecification::Template;
				}
			}
		}
		else
		{
			bIsTemplate = TemplateSpecification == ENiagaraScriptTemplateSpecification::Template;
		}

		bool bFoundLibraryTag = Item.GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, bExposeToLibrary), bInLibrary);

		if (bFoundLibraryTag == false)
		{
			if (Item.IsAssetLoaded())
			{
				UNiagaraEmitter* EmitterAsset = static_cast<UNiagaraEmitter*>(Item.GetAsset());
				if (EmitterAsset != nullptr)
				{
					bInLibrary = EmitterAsset->bExposeToLibrary;
				}
			}
		}


		if (TabOptions.GetOnlyShowTemplates()
			|| (bFoundTemplateScriptTag && bIsTemplate)
			|| (bFoundLibraryTag && bInLibrary))
		{
			return
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(15, 3, 10, 3)
				[
					SNew(SBox)
					.WidthOverride(ThumbnailSize)
					.HeightOverride(ThumbnailSize)
					[
						AssetThumbnail->MakeThumbnailWidget(ThumbnailConfig)
					]
				]
				+ SHorizontalBox::Slot()
				.Padding(5, 0, 5, 0)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.AssetPickerBoldAssetNameText")
						.Text(FText::FromString(FName::NameToDisplayString(Item.AssetName.ToString(), false)))
						.HighlightText_Lambda([this]() { return ItemSelector->GetFilterText(); })
					]
					+ SVerticalBox::Slot()
					[
						SNew(STextBlock)
						.Text(AssetDescription)
						.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.AssetPickerAssetNameText")
						.AutoWrapText(true)
					]
				];
		}

		return GenerateWidgetForItem_Generic();
	};

	if(ViewOptions.GetExpandTemplateAndLibraryAssets())
	{
		return GenerateWidgetForItem_ExpandTemplateAndLibraryItems();
	}

	return GenerateWidgetForItem_Generic();
}

void SNiagaraAssetPickerList::OnItemActivated(const FAssetData& Item)
{
	OnTemplateAssetActivated.ExecuteIfBound(Item);
}

bool SNiagaraAssetPickerList::DoesItemPassCustomFilter(const FAssetData& Item)
{
	bool bDoesPassFilter = true;
	if(CustomFilter.IsBound())
	{
		bDoesPassFilter &= CustomFilter.Execute(Item);
	}

	bDoesPassFilter &= SourceFilterBox->IsFilterActive(FNiagaraEditorUtilities::GetScriptSource(Item).Key);

	if (bLibraryOnly == true)
	{
		bool bInLibrary = false;
		Item.GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, bExposeToLibrary), bInLibrary);
		bDoesPassFilter &= bInLibrary;
	}

	if(bUseActiveTab)
	{
		ENiagaraScriptTemplateSpecification TemplateSpecification;
		FNiagaraEditorUtilities::GetTemplateSpecificationFromTag(Item, TemplateSpecification);
		
		if(ActiveTab == ENiagaraScriptTemplateSpecification::Template)
		{
			bDoesPassFilter &= TemplateSpecification == ENiagaraScriptTemplateSpecification::Template;
		}
		else if(ActiveTab == ENiagaraScriptTemplateSpecification::None)
		{
			bDoesPassFilter &= TemplateSpecification == ENiagaraScriptTemplateSpecification::None;
		}
		else if(ActiveTab == ENiagaraScriptTemplateSpecification::Behavior)
		{
			bDoesPassFilter &= TemplateSpecification == ENiagaraScriptTemplateSpecification::Behavior;
		}
	}

	return bDoesPassFilter;
}

FText SNiagaraAssetPickerList::GetFilterText() const
{
	return ItemSelector->GetFilterText();
}

void SNiagaraAssetPickerList::TriggerRefresh(const TMap<EScriptSource, bool>& SourceState)
{
	ItemSelector->RefreshAllCurrentItems();

	TArray<bool> States;
	SourceState.GenerateValueArray(States);

	int32 NumActive = 0;
	for(bool& State : States)
	{
		if(State == true)
		{
			NumActive++;
		}
	}

	ItemSelector->ExpandTree();
}

void SNiagaraAssetPickerList::OnTabActivated(ECheckBoxState NewState, ENiagaraScriptTemplateSpecification AssetTab)
{
	if(ActiveTab != AssetTab)
	{
		ActiveTab = AssetTab;
		CachedActiveTab = ActiveTab;
		RefreshAll();
		ExpandTree();
	}
}

FSlateColor SNiagaraAssetPickerList::GetBackgroundColor(ENiagaraScriptTemplateSpecification TemplateSpecification) const
{
	if(ActiveTab == TemplateSpecification)
	{
		return FCoreStyle::Get().GetSlateColor("SelectionColor");
	}

	return FLinearColor::Transparent;
}

FSlateColor SNiagaraAssetPickerList::GetTabForegroundColor(ENiagaraScriptTemplateSpecification TemplateSpecification) const
{
	if(ActiveTab == TemplateSpecification)
	{
		return FLinearColor::Black;
	}

	return FLinearColor::White;
}

void SNiagaraAssetPickerList::LibraryCheckBoxStateChanged(ECheckBoxState InCheckbox)
{
	bLibraryOnly = (InCheckbox == ECheckBoxState::Checked);
	RefreshAll();
	ExpandTree();
}

ECheckBoxState SNiagaraAssetPickerList::GetLibraryCheckBoxState() const
{
	return bLibraryOnly ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

#undef LOCTEXT_NAMESPACE
