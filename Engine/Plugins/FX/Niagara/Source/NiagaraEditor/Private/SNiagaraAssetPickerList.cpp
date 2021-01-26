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
#include "ThumbnailRendering/ThumbnailManager.h"

#define LOCTEXT_NAMESPACE "SNiagaraAssetSelector"

FText SNiagaraAssetPickerList::NiagaraPluginCategory = LOCTEXT("NiagaraCategory", "Engine (Niagara Plugin)");
FText SNiagaraAssetPickerList::ProjectCategory = LOCTEXT("ProjectCategory", "Project");
FText SNiagaraAssetPickerList::TemplateCategory = LOCTEXT("Template", "Template");
FText SNiagaraAssetPickerList::LibraryCategory = LOCTEXT("Library", "Library");
FText SNiagaraAssetPickerList::NonLibraryCategory = LOCTEXT("NotInLibrary", "Not in Library");
FText SNiagaraAssetPickerList::UncategorizedCategory = LOCTEXT("Uncategorized", "Uncategorized");

void SNiagaraAssetPickerList::Construct(const FArguments& InArgs, UClass* AssetClass)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> EmitterAssets;
	AssetRegistryModule.Get().GetAssetsByClass(AssetClass->GetFName(), EmitterAssets);

	OnTemplateAssetActivated = InArgs._OnTemplateAssetActivated;
	ViewOptions = InArgs._ViewOptions;

	TArray<FAssetData> EmittersToShow;
	if (ViewOptions.GetOnlyShowTemplates())
	{
		for(FAssetData& EmitterAsset :EmitterAssets)
		{ 
			bool bTemplateEmitter = false;
			EmitterAsset.GetTagValue("bIsTemplateAsset", bTemplateEmitter);
			if (bTemplateEmitter)
			{
				EmittersToShow.Add(EmitterAsset);
			}
		}
	}
	else
	{
		EmittersToShow = EmitterAssets;
	}

	ChildSlot
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
		.OnDoesItemPassCustomFilter(InArgs._OnDoesAssetPassCustomFilter)
		.RefreshItemSelectorDelegates(InArgs._RefreshItemSelectorDelegates)
		.ClickActivateMode(InArgs._ClickActivateMode)
	];
}

void SNiagaraAssetPickerList::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
}

TArray<FAssetData> SNiagaraAssetPickerList::GetSelectedAssets() const
{
	return ItemSelector->GetSelectedItems();
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
				UNiagaraEmitter* EmitterAsset = static_cast<UNiagaraEmitter*>(Item.GetAsset());
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

	auto AddTemplateCategory = [&Categories, &Item, this]() {
		bool bIsTemplate = false;
		bool bFoundTemplateScriptTag = Item.GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, bIsTemplateAsset), bIsTemplate);
		if (bFoundTemplateScriptTag == false)
		{
			if (Item.IsAssetLoaded())
			{
				UNiagaraEmitter* EmitterAsset = static_cast<UNiagaraEmitter*>(Item.GetAsset());
				if (EmitterAsset != nullptr)
				{
					bIsTemplate = EmitterAsset->bIsTemplateAsset;
				}
			}
		}

		if (bIsTemplate)
		{
			Categories.Add(TemplateCategory);
		}
		else
		{
			Categories.Add(LOCTEXT("NonTemplateEmitters", "Non-Template"));
		}
	};

	auto AddLibraryCategory = [&Categories, &Item, this]() {
		bool bInLibrary = false;
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

	if(ViewOptions.GetCategorizeTemplateAssets())
	{
		AddTemplateCategory();
	}
	if (ViewOptions.GetCategorizeLibraryAssets())
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
	if (ViewOptions.GetOnlyShowTemplates())
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
			// Template first
			if (CategoryA.CompareTo(TemplateCategory) == 0)
			{
				return true;
			}
			if (CategoryB.CompareTo(TemplateCategory) == 0)
			{
				return false;
			}

			// Library second.
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
	TSharedRef<FAssetThumbnail> AssetThumbnail = MakeShared<FAssetThumbnail>(Item, ThumbnailSize, ThumbnailSize, UThumbnailManager::Get().GetSharedThumbnailPool());
	FAssetThumbnailConfig ThumbnailConfig;
	ThumbnailConfig.bAllowFadeIn = false;
	

	auto GenerateWidgetForItem_Generic = [&Item, &ThumbnailConfig, &AssetThumbnail, this]()->TSharedRef<SWidget> {
		return 	SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(15, 3, 3, 3)
			[
				SNew(SBox)
				.WidthOverride(32.0)
				.HeightOverride(32.0)
				[
					AssetThumbnail->MakeThumbnailWidget(ThumbnailConfig)
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
		bool bFoundTemplateScriptTag = Item.GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraEmitter, bIsTemplateAsset), bIsTemplate);

		if (bFoundTemplateScriptTag == false)
		{
			if (Item.IsAssetLoaded())
			{
				UNiagaraEmitter* EmitterAsset = static_cast<UNiagaraEmitter*>(Item.GetAsset());
				if (EmitterAsset != nullptr)
				{
					bIsTemplate = EmitterAsset->bIsTemplateAsset;
				}
			}
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


		if (ViewOptions.GetOnlyShowTemplates()
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

FText SNiagaraAssetPickerList::GetFilterText() const
{
	return ItemSelector->GetFilterText();
}

#undef LOCTEXT_NAMESPACE
