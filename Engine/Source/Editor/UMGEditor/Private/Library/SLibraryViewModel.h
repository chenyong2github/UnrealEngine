// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WidgetBlueprintEditor.h"
#include "AssetData.h"

#include "IContentBrowserSingleton.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "SPaletteViewModel.h"

class FWidgetTemplate;
class FWidgetBlueprintEditor;
class UWidgetBlueprint;
class SAssetView;
class SView;

class FWidgetTemplateListViewModel : public FWidgetViewModel
{
public:
	FWidgetTemplateListViewModel();

	/** Builds list view for given templates */
	void ConstructListView(TArray<TSharedPtr<FWidgetTemplate>> InTemplates);

	//~ Begin FWidgetViewModel Interface
	virtual FText GetName() const override;
	virtual bool IsTemplate() const override;
	virtual void GetFilterStrings(TArray<FString>& OutStrings) const override;
	virtual bool HasFilteredChildTemplates() const override;
	virtual TSharedRef<ITableRow> BuildRow(const TSharedRef<STableViewBase>& OwnerTable) override;
	//~ End FWidgetViewModel Interface

	void SetViewType(EAssetViewType::Type ViewType);

	void SetThumbnailSize(EThumbnailSize ThumbnailSize);

	void SetSearchText(const FText& InSearchText);
	FText GetSearchText() const;

	TArray<TSharedPtr<FWidgetTemplate>> Templates;
	TSharedPtr<FAssetFilterCollectionType> TemplatesFilter;
private:

	/** The asset view widget */
	TSharedPtr<SAssetView> AssetViewPtr;

	/** Filter we use to narrow down the libary to select widgets asset view */
	TSharedPtr<class FFrontendFilter_Text> WidgetTextFilter;

	/** Filter we forward our search text to for the asset view */
	TSharedPtr<class FFrontendFilter_Text> SearchFilter;

	/** Lowercase string used by widget text filter */
	FString CachedLowercaseWidgetFilter;
};

class FLibraryViewModel : public FWidgetCatalogViewModel
{
public:
	FLibraryViewModel(TSharedPtr<FWidgetBlueprintEditor> InBlueprintEditor) : FWidgetCatalogViewModel(InBlueprintEditor) { }

	//~ Begin FWidgetCatalogViewModel Interface
	virtual void BuildWidgetTemplateCategory(FString& Category, TArray<TSharedPtr<FWidgetTemplate>>& Templates) override;
	virtual void AddToFavorites(const FWidgetTemplateViewModel* WidgetTemplateViewModel) override;
	virtual void RemoveFromFavorites(const FWidgetTemplateViewModel* WidgetTemplateViewModel) override;
	virtual void SetSearchText(const FText& InSearchText) override;
	//~ End FWidgetCatalogViewModel Interface

	ViewModelsArray& GetWidgetViewModels() { return WidgetViewModels; }
	ViewModelsArray& GetWidgetTemplateListViewModels() { return WidgetTemplateListViewModels; }

protected:
	//~ Begin FWidgetCatalogViewModel Interface
	virtual void BuildWidgetList() override;
	//~ End FWidgetCatalogViewModel Interface
	
	/** Widget template list view models associated with each root. */
	ViewModelsArray WidgetTemplateListViewModels;
};

