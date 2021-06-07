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

void SNiagaraAssetPickerList::Construct(const FArguments& InArgs, UClass* AssetClass)
{
	AssetThumbnailPool = MakeShareable(new FAssetThumbnailPool(24));

	OnTemplateAssetActivated = InArgs._OnTemplateAssetActivated;
	ViewOptions = InArgs._ViewOptions;
	TabOptions = InArgs._TabOptions;
	CustomFilter = InArgs._OnDoesAssetPassCustomFilter;
	bLibraryOnly = InArgs._bLibraryOnly;
	
	TArray<FAssetData> EmittersToShow = GetAssetDataForSelector(AssetClass);

	SNiagaraFilterBox::FFilterOptions FilterOptions;
	FilterOptions.SetAddTemplateFilter(true);
	FilterOptions.SetTabOptions(TabOptions);

	FilterBox = SNew(SNiagaraFilterBox, FilterOptions)
	.bLibraryOnly(this, &SNiagaraAssetPickerList::GetLibraryCheckBoxState)
	.OnLibraryOnlyChanged(this, &SNiagaraAssetPickerList::LibraryCheckBoxStateChanged)
	.OnSourceFiltersChanged(this, &SNiagaraAssetPickerList::TriggerRefresh)
	.OnTabActivated(this, &SNiagaraAssetPickerList::TriggerRefreshFromTabs);
	
	if(!ViewOptions.GetAddLibraryOnlyCheckbox())
	{
		FilterOptions.SetAddLibraryFilter(false);
	}
	
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(3.f)
		[
			FilterBox.ToSharedRef()
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

	bDoesPassFilter &= FilterBox->IsSourceFilterActive(FNiagaraEditorUtilities::GetScriptSource(Item).Key);

	if (bLibraryOnly == true)
	{
		bool bInLibrary = false;
		Item.GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, bExposeToLibrary), bInLibrary);
		bDoesPassFilter &= bInLibrary;
	}

	ENiagaraScriptTemplateSpecification ActiveTab = ENiagaraScriptTemplateSpecification::None;
	if(FilterBox->GetActiveTemplateTab(ActiveTab))
	{
		ENiagaraScriptTemplateSpecification TemplateSpecification;
		FNiagaraEditorUtilities::GetTemplateSpecificationFromTag(Item, TemplateSpecification);

		bDoesPassFilter &= TemplateSpecification == ActiveTab;
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

void SNiagaraAssetPickerList::TriggerRefreshFromTabs(ENiagaraScriptTemplateSpecification Tab)
{
	RefreshAll();
	ExpandTree();
}

void SNiagaraAssetPickerList::LibraryCheckBoxStateChanged(bool bInLibraryOnly)
{
	bLibraryOnly = bInLibraryOnly;
	RefreshAll();
	ExpandTree();
}

bool SNiagaraAssetPickerList::GetLibraryCheckBoxState() const
{
	return bLibraryOnly;
}

#undef LOCTEXT_NAMESPACE
