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

#define LOCTEXT_NAMESPACE "SNiagaraAssetSelector"

void SNiagaraAssetPickerList::Construct(const FArguments& InArgs, UClass* AssetClass)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> EmitterAssets;
	AssetRegistryModule.Get().GetAssetsByClass(AssetClass->GetFName(), EmitterAssets);

	NiagaraPluginCategory = LOCTEXT("NiagaraCategory", "Engine (Niagara Plugin)");
	ProjectCategory = LOCTEXT("ProjectCategory", "Project");
	TemplateCategory = LOCTEXT("Template", "Template");
	LibraryCategory = LOCTEXT("Library", "Library");
	NonLibraryCategory = LOCTEXT("NotInLibrary", "Not in Library");
	AssetThumbnailPool = MakeShareable(new FAssetThumbnailPool(24));

	OnTemplateAssetActivated = InArgs._OnTemplateAssetActivated;
	bTemplateOnly = InArgs._bTemplateOnly;
	TArray<FAssetData> EmittersToShow;
	for (const FAssetData& EmitterAsset : EmitterAssets)
	{
		bool bShowEmitter = false;
		EmitterAsset.GetTagValue("bIsTemplateAsset", bShowEmitter);
		if (bShowEmitter || !bTemplateOnly)
		{
			EmittersToShow.Add(EmitterAsset);
		}
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
	];
}

void SNiagaraAssetPickerList::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	AssetThumbnailPool->Tick(InDeltaTime);
}

TArray<FAssetData> SNiagaraAssetPickerList::GetSelectedAssets() const
{
	return ItemSelector->GetSelectedItems();
}

TArray<FText> SNiagaraAssetPickerList::OnGetCategoriesForItem(const FAssetData& Item)
{
	TArray<FText> Categories;
	if (bTemplateOnly)
	{
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
	}
	else
	{

		bool bIsTemplate;
		Item.GetTagValue("bIsTemplateAsset", bIsTemplate);
		if (bIsTemplate)
		{
			Categories.Add(TemplateCategory);
		}
		else
		{
			Categories.Add(LOCTEXT("NonTemplateEmitters", "Non-Template"));
		}
	}

	bool bIsLibrary;
	bool bFoundLibScriptTag = Item.GetTagValue("bExposeToLibrary", bIsLibrary);

	if (bFoundLibScriptTag && bIsLibrary)
	{
		Categories.Add(LibraryCategory);
	}
	else
	{
		Categories.Add(NonLibraryCategory);
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
	if (bTemplateOnly)
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
	return Item.AssetName.ToString().Contains(FilterText.ToString());
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

	FText AssetDescription;
	Item.GetTagValue("TemplateAssetDescription", AssetDescription);

	bool bIsTemplate;
	bool bIsLibrary;
	Item.GetTagValue("bIsTemplateAsset", bIsTemplate);
	bool bFoundLibScriptTag = Item.GetTagValue("bExposeToLibrary", bIsLibrary);

	if (bTemplateOnly
		|| (bIsTemplate)
		|| (bFoundLibScriptTag && bIsLibrary))
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
		];
}

void SNiagaraAssetPickerList::OnItemActivated(const FAssetData& Item)
{
	OnTemplateAssetActivated.ExecuteIfBound(Item);
}

#undef LOCTEXT_NAMESPACE
