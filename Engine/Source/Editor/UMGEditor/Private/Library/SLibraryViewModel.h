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

	virtual FText GetName() const override;

	virtual bool IsTemplate() const override;

	virtual void GetFilterStrings(TArray<FString>& OutStrings) const override;

	virtual TSharedRef<ITableRow> BuildRow(const TSharedRef<STableViewBase>& OwnerTable) override;

	// FReply OnDraggingWidgetTemplateItem(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

	TArray<TSharedPtr<FWidgetTemplate>> Templates;
	TSharedPtr<FAssetFilterCollectionType> TemplatesFilter;
private:

	/** The asset view widget */
	TSharedPtr<SAssetView> AssetViewPtr;
};

class FLibraryViewModel : public FFavortiesViewModel
{
public:

	DECLARE_MULTICAST_DELEGATE(FOnUpdating)
	DECLARE_MULTICAST_DELEGATE(FOnUpdated)

public:
	FLibraryViewModel(TSharedPtr<FWidgetBlueprintEditor> InBlueprintEditor);
	virtual ~FLibraryViewModel();

	/** Register the View Model to events that should trigger a update of the Library*/
	void RegisterToEvents();

	/** Update the view model if needed and returns true if it did. */
	void Update();

	/** Returns true if the view model needs to be updated */
	bool NeedUpdate() const { return bRebuildRequested; }
	   
	/** Add the widget template to the list of favorites */
	virtual void AddToFavorites(const FWidgetTemplateViewModel* WidgetTemplateViewModel) override;

	/** Remove the widget template to the list of favorites */
	virtual void RemoveFromFavorites(const FWidgetTemplateViewModel* WidgetTemplateViewModel) override;

	typedef TArray< TSharedPtr<FWidgetViewModel> > ViewModelsArray;
	ViewModelsArray& GetWidgetViewModels() { return WidgetViewModels; }

	/** Fires before the view model is updated */
	FOnUpdating OnUpdating;

	/** Fires after the view model is updated */
	FOnUpdated OnUpdated;

private:
	FLibraryViewModel() {};

	UWidgetBlueprint* GetBlueprint() const;

	void BuildWidgetList();
	void BuildClassWidgetList();

	static bool FilterAssetData(FAssetData &BPAssetData);

	void AddWidgetTemplate(TSharedPtr<FWidgetTemplate> Template);

	/** Called when a Blueprint is recompiled and live objects are swapped out for replacements */
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap);

	/** Requests a rebuild of the widget list if a widget blueprint was compiled */
	void OnBlueprintReinstanced();

	/** Called when the favorite list is changed */
	void OnFavoritesUpdated();

	/** Requests a rebuild of the widget list */
	void OnReloadComplete(EReloadCompleteReason Reason);

	/** Requests a rebuild of the widget list if a widget blueprint was deleted */
	void HandleOnAssetsDeleted(const TArray<UClass*>& DeletedAssetClasses);
	
	TWeakPtr<class FWidgetBlueprintEditor> BlueprintEditor;

	typedef TArray<TSharedPtr<FWidgetTemplate>> WidgetTemplateArray;
	TMap<FString, WidgetTemplateArray> WidgetTemplateCategories;

	/** The source root view models for the tree. */
	ViewModelsArray WidgetViewModels;
	   
	/** Controls rebuilding the list of spawnable widgets */
	bool bRebuildRequested;

	TSharedPtr<FWidgetHeaderViewModel> FavoriteHeader;
};

