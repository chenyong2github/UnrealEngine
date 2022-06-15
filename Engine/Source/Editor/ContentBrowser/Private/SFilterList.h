// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/ARFilter.h"
#include "FrontendFilterBase.h"
#include "Filters/SAssetFilterBar.h"

class UToolMenu;
struct FToolMenuSection;
class SFilter;
class SWrapBox;
enum class ECheckBoxState : uint8;

/**
 * A list of filters currently applied to an asset view.
 */
class SFilterList : public SAssetFilterBar<FAssetFilterType>
{
public:

	DECLARE_DELEGATE_RetVal( TSharedPtr<SWidget>, FOnGetContextMenu );

	using FOnFilterChanged = typename SAssetFilterBar<FAssetFilterType>::FOnFilterChanged;

	SLATE_BEGIN_ARGS( SFilterList ){}

		/** Called when an asset is right clicked */
		SLATE_EVENT( FOnGetContextMenu, OnGetContextMenu )

		/** Delegate for when filters have changed */
		SLATE_EVENT( FOnFilterChanged, OnFilterChanged )

		/** The filter collection used to further filter down assets returned from the backend */
		SLATE_ARGUMENT( TSharedPtr<FAssetFilterCollectionType>, FrontendFilters)

		/** An array of classes to filter the menu by */
		SLATE_ARGUMENT( TArray<UClass*>, InitialClassFilters)

		/** Custom front end filters to be displayed */
		SLATE_ARGUMENT( TArray< TSharedRef<FFrontendFilter> >, ExtraFrontendFilters )

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct( const FArguments& InArgs );

	/** Set the check box state of the specified frontend filter (in the filter drop down) and pin/unpin a filter widget on/from the filter bar. When a filter is pinned (was not already pinned), it is activated and deactivated when unpinned. */
	void SetFrontendFilterCheckState(const TSharedPtr<FFrontendFilter>& InFrontendFilter, ECheckBoxState CheckState);

	/** Returns the check box state of the specified frontend filter (in the filter drop down). This tells whether the filter is pinned or not on the filter bar, but not if filter is active or not. @see IsFrontendFilterActive(). */
	ECheckBoxState GetFrontendFilterCheckState(const TSharedPtr<FFrontendFilter>& InFrontendFilter) const;

	/** Returns true if the specified frontend filter is both checked (pinned on the filter bar) and active (contributing to filter the result). */
	bool IsFrontendFilterActive(const TSharedPtr<FFrontendFilter>& InFrontendFilter) const;

	/** Retrieve a specific frontend filter */
	TSharedPtr<FFrontendFilter> GetFrontendFilter(const FString& InName) const;

	/** Handler for when the floating add filter button was clicked */
	TSharedRef<SWidget> ExternalMakeAddFilterMenu(EAssetTypeCategories::Type MenuExpansion = EAssetTypeCategories::Basic);

	/** Disables any active filters that would hide the supplied items */
	void DisableFiltersThatHideItems(TArrayView<const FContentBrowserItem> ItemList);

	/** Saves any settings to config that should be persistent between editor sessions */
	void SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const;

	/** Loads any settings to config that should be persistent between editor sessions */
	void LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString);

	virtual FReply OnMouseButtonUp( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override;

	/** Returns the class filters specified at construction using argument 'InitialClassFilters'. */
	const TArray<UClass*>& GetInitialClassFilters();

protected:
	
	/** Handler for when the add filter button was clicked */
	TSharedRef<SWidget> MakeAddFilterMenu() override;

private:

	// Exists for backwards compatibility with ExternalMakeAddFilterMenu
	TSharedRef<SWidget> MakeAddFilterMenu(EAssetTypeCategories::Type MenuExpansion = EAssetTypeCategories::Basic);

	void PopulateAddFilterMenu_Internal(UToolMenu* Menu);
	
private:

	/** List of classes that our filters must match */
	TArray<UClass*> InitialClassFilters;

	/** Delegate for getting the context menu. */
	FOnGetContextMenu OnGetContextMenu;

	/** Delegate for when filters have changed */
	FOnFilterChanged OnFilterChanged;

	/** A reference to AllFrontEndFilters so we can access the filters as FFrontEndFilter instead of FFilterBase<FAssetFilterType> */
	TArray< TSharedRef<FFrontendFilter> > AllFrontendFilters_Internal;
};
