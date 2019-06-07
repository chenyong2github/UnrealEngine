// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WidgetBlueprintEditor.h"
#include "AssetData.h"

#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"

class FWidgetTemplate;
class FWidgetBlueprintEditor;
class UWidgetBlueprint;
class SPaletteView;

/** View model for the items in the widget template list */
class FWidgetViewModel : public TSharedFromThis<FWidgetViewModel>
{
public:
	virtual ~FWidgetViewModel() { }

	virtual FText GetName() const = 0;

	virtual bool IsTemplate() const = 0;

	/** @param OutStrings - Returns an array of strings used for filtering/searching this item. */
	virtual void GetFilterStrings(TArray<FString>& OutStrings) const = 0;

	virtual TSharedRef<ITableRow> BuildRow(const TSharedRef<STableViewBase>& OwnerTable) = 0;

	virtual void GetChildren(TArray< TSharedPtr<FWidgetViewModel> >& OutChildren)
	{
	}

	/** Return true if the widget is a favorite */
	virtual bool IsFavorite() const { return false; }

	/** Set the favorite flag */
	virtual void SetFavorite()
	{
	}

	virtual bool ShouldForceExpansion() const { return false; }
};

class FWidgetTemplateViewModel : public FWidgetViewModel
{
public:
	FWidgetTemplateViewModel();

	virtual FText GetName() const override;

	virtual bool IsTemplate() const override;

	virtual void GetFilterStrings(TArray<FString>& OutStrings) const override;

	virtual TSharedRef<ITableRow> BuildRow(const TSharedRef<STableViewBase>& OwnerTable) override;

	FReply OnDraggingWidgetTemplateItem(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

	/** Add the widget template to the list of favorites */
	void AddToFavorites();

	/** Remove the widget template from the list of favorites */
	void RemoveFromFavorites();

	/** Return true if the widget is a favorite */
	virtual bool IsFavorite() const override { return bIsFavorite; }

	/** Set the favorite flag */
	virtual void SetFavorite() override { bIsFavorite = true; }

	TSharedPtr<FWidgetTemplate> Template;
	FPaletteViewModel* PaletteViewModel;
private:
	/** True is the widget is a favorite. It's keep as a state to prevent a search in the favorite list. */
	bool bIsFavorite;
};

class FWidgetHeaderViewModel : public FWidgetViewModel
{
public:
	virtual ~FWidgetHeaderViewModel()
	{
	}

	virtual FText GetName() const override
	{
		return GroupName;
	}

	virtual bool IsTemplate() const override
	{
		return false;
	}

	virtual void GetFilterStrings(TArray<FString>& OutStrings) const override
	{
		// Headers should never be included in filtering to avoid showing a header with all of
		// it's widgets filtered out, so return an empty filter string.
	}

	virtual TSharedRef<ITableRow> BuildRow(const TSharedRef<STableViewBase>& OwnerTable) override;

	virtual void GetChildren(TArray< TSharedPtr<FWidgetViewModel> >& OutChildren) override;

	virtual bool ShouldForceExpansion() const { return bForceExpansion; }

	void SetForceExpansion(bool bInForceExpansion) { bForceExpansion = bInForceExpansion; }

	FText GroupName;
	TArray< TSharedPtr<FWidgetViewModel> > Children;

private:

	bool bForceExpansion = false;
};

class FPaletteViewModel: public TSharedFromThis<FPaletteViewModel>
{
public:

	DECLARE_MULTICAST_DELEGATE(FOnUpdating)
	DECLARE_MULTICAST_DELEGATE(FOnUpdated)

public:
	FPaletteViewModel(TSharedPtr<FWidgetBlueprintEditor> InBlueprintEditor);
	~FPaletteViewModel();

	/** Register the View Model to events that should trigger a update of the Palette*/
	void RegisterToEvents();

	/** Update the view model if needed and returns true if it did. */
	void Update();

	/** Returns true if the view model needs to be updated */
	bool NeedUpdate() const { return bRebuildRequested; }
	   
	/** Add the widget template to the list of favorites */
	void AddToFavorites(const FWidgetTemplateViewModel* WidgetTemplateViewModel);

	/** Remove the widget template to the list of favorites */
	void RemoveFromFavorites(const FWidgetTemplateViewModel* WidgetTemplateViewModel);

	typedef TArray< TSharedPtr<FWidgetViewModel> > ViewModelsArray;
	ViewModelsArray& GetWidgetViewModels() { return WidgetViewModels; }

	void SetSearchText(const FText& inSearchText) { SearchText = inSearchText; }
	FText GetSearchText() const { return SearchText; }

	/** Fires before the view model is updated */
	FOnUpdating OnUpdating;

	/** Fires after the view model is updated */
	FOnUpdated OnUpdated;

private:
	FPaletteViewModel() {};

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
	void HandleOnHotReload(bool bWasTriggeredAutomatically);

	/** Requests a rebuild of the widget list if a widget blueprint was deleted */
	void HandleOnAssetsDeleted(const TArray<UClass*>& DeletedAssetClasses);
	
	TWeakPtr<class FWidgetBlueprintEditor> BlueprintEditor;

	typedef TArray<TSharedPtr<FWidgetTemplate>> WidgetTemplateArray;
	TMap<FString, WidgetTemplateArray> WidgetTemplateCategories;

	/** The source root view models for the tree. */
	ViewModelsArray WidgetViewModels;
	   
	/** Controls rebuilding the list of spawnable widgets */
	bool bRebuildRequested;

	FText SearchText;

	TSharedPtr<FWidgetHeaderViewModel> FavoriteHeader;
};

