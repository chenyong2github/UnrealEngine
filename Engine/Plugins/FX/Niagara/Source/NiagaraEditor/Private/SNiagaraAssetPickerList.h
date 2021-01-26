// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SItemSelector.h"
#include "AssetData.h"

typedef SItemSelector<FText, FAssetData> SNiagaraAssetItemSelector;

class FAssetThumbnailPool;

struct FNiagaraAssetPickerListViewOptions
{
public:
	FNiagaraAssetPickerListViewOptions()
		: bOnlyShowTemplates(false)
		, bCategorizeAssetsByAssetPath(false)
		, bExpandTemplateAndLibraryAssets(false)
		, bCategorizeTemplateAssets(false)
		, bCategorizeLibraryAssets(false)
		, bCategorizeUserDefinedCategory(false)
	{};

	// Only showing template assets also implies categorizing by asset path; enforce via public setter.
	void SetOnlyShowTemplatesAndCategorizeByAssetPath(bool bOnlyShowTemplatesAndCategorizeByAssetPath) {
		bOnlyShowTemplates = bOnlyShowTemplatesAndCategorizeByAssetPath;
		bCategorizeAssetsByAssetPath = bOnlyShowTemplatesAndCategorizeByAssetPath;
	};

	void SetExpandTemplateAndLibraryAssets(bool bInExpandTemplateAndLibraryAssets) {bExpandTemplateAndLibraryAssets = bInExpandTemplateAndLibraryAssets; };
	void SetCategorizeTemplateAssets(bool bInCategorizeTemplateAssets) {bCategorizeTemplateAssets = bInCategorizeTemplateAssets; };
	void SetCategorizeLibraryAssets(bool bInCategorizeLibraryAssets) {bCategorizeLibraryAssets = bInCategorizeLibraryAssets; };
	void SetCategorizeUserDefinedCategory(bool bInCategorizeUserDefinedCategory) {bCategorizeUserDefinedCategory = bInCategorizeUserDefinedCategory; };

	bool GetOnlyShowTemplates() const {return bOnlyShowTemplates; };
	bool GetCategorizeAssetsByAssetPath() const {return bCategorizeAssetsByAssetPath; };
	bool GetExpandTemplateAndLibraryAssets() const {return bExpandTemplateAndLibraryAssets; };
	bool GetCategorizeTemplateAssets() const {return bCategorizeTemplateAssets; };
	bool GetCategorizeLibraryAssets() const { return bCategorizeLibraryAssets; };
	bool GetCategorizeUserDefinedCategory() const {return bCategorizeUserDefinedCategory; };

private:
	// If true, only present assets marked as templates. Otherwise present all assets.
	bool bOnlyShowTemplates;

	// If true, categorize assets by their asset path, relative to the Niagara plugin directory, the project directory, and any other directory.
	bool bCategorizeAssetsByAssetPath;

	// If true, expand the thumbnails for assets marked as templates or library only.
	bool bExpandTemplateAndLibraryAssets; 

	// If true, categorize assets marked as templates.
	bool bCategorizeTemplateAssets;

	// If true, categorize assets marked as library only.
	bool bCategorizeLibraryAssets;

	// If true, categorize assets that have a user defined category.
	bool bCategorizeUserDefinedCategory;
};

class NIAGARAEDITOR_API SNiagaraAssetPickerList : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnTemplateAssetActivated, const FAssetData&);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FOnDoesAssetPassCustomFilter, const FAssetData&)

public:
	SLATE_BEGIN_ARGS(SNiagaraAssetPickerList) 
		: _bAllowMultiSelect(false)
		, _ClickActivateMode(EItemSelectorClickActivateMode::DoubleClick)
	{}
		// Callback for when an asset is activated.
		SLATE_EVENT(FOnTemplateAssetActivated, OnTemplateAssetActivated);

		// Whether to allow multi-selecting assets.
		SLATE_ARGUMENT(bool, bAllowMultiSelect);

		/** An optional delegate which is called to check if an asset should be filtered out by external code. Return false to exclude the asset from the view. */
		SLATE_EVENT(FOnDoesAssetPassCustomFilter, OnDoesAssetPassCustomFilter);

		/** An optional array of delegates to refresh the item selector view when executed. */
		SLATE_ARGUMENT(TArray<FRefreshItemSelectorDelegate*>, RefreshItemSelectorDelegates);

		// Arguments describing how to display, filter and categorize items of the asset picker.
		SLATE_ARGUMENT(FNiagaraAssetPickerListViewOptions, ViewOptions);

		/** Whether or not a single click activates an item. */
		SLATE_ARGUMENT(EItemSelectorClickActivateMode, ClickActivateMode);
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UClass* AssetClass);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	TArray<FAssetData> GetSelectedAssets() const;

private:
	TArray<FText> OnGetCategoriesForItem(const FAssetData& Item);

	bool OnCompareCategoriesForEquality(const FText& CategoryA, const FText& CategoryB) const;

	bool OnCompareCategoriesForSorting(const FText& CategoryA, const FText& CategoryB) const;

	bool OnCompareItemsForSorting(const FAssetData& ItemA, const FAssetData& ItemB) const;

	bool OnDoesItemMatchFilterText(const FText& FilterText, const FAssetData& Item);

	TSharedRef<SWidget> OnGenerateWidgetForCategory(const FText& Category);

	TSharedRef<SWidget> OnGenerateWidgetForItem(const FAssetData& Item);

	void OnItemActivated(const FAssetData& Item);

	FText GetFilterText() const;
private:
	TSharedPtr<SNiagaraAssetItemSelector> ItemSelector;
	static FText NiagaraPluginCategory;
	static FText ProjectCategory;
	static FText TemplateCategory;
	static FText LibraryCategory;
	static FText NonLibraryCategory;
	static FText UncategorizedCategory;
	FOnTemplateAssetActivated OnTemplateAssetActivated;
	FOnDoesAssetPassCustomFilter OnDoesAssetPassCustomFilter;
	FNiagaraAssetPickerListViewOptions ViewOptions;
};
