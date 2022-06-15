// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SlateGlobals.h"
#include "Logging/LogMacros.h"
#include "Logging/LogVerbosity.h"

#include "Filters/SAssetFilterBar.h"
#include "Filters/AssetFilter.h"
#include "Filters/FilterBarConfig.h"

/** A non-templated base class for SFilterBar, where functionality that does not depend on the type of item
 *  being filtered lives
 */
class EDITORWIDGETS_API FFilterBarBase
{
protected:

	/** Get a mutable version of this filter bar's config */
	FFilterBarSettings* GetMutableConfig();

	/** Get a const version of this filter bar's config */
	const FFilterBarSettings* GetConstConfig() const;

	/** Save this filte bar's data to the config file */
	void SaveConfig();

	/** Initialize and load this filter bar's config */
	void InitializeConfig();

protected:

	/* Unique name for this filter bar */
	FName FilterBarIdentifier;
};

/**
* A Filter Bar widget, which can be used to filter items of type [FilterType] given a list of custom filters
* along with built in support for Asset Type filters
* @see SBasicFilterBar if you don't want Asset Type filters, or if you want a filter bar usable in non-editor situations
* NOTE: You will need to also add "ToolWidgets" as a dependency to your module to use this widget
* NOTE: The filter functions create copies, so you want to use SFilterBar<TSharedPtr<ItemType>> etc instead of SFilterBar<ItemType> when possible
* NOTE: The user must specify one of the following:
* a) OnConvertItemToAssetData:   A conversion function to convert FilterType to an FAssetData. Specifying this will filter the asset type
*							     through the AssetRegistry, which is potentially more thorough and fast
* b) OnCompareItemWithClassName: A comparison function to check if an Item (FilterType) is the same as an AssetType (represented by an FName).
*								 This allows to user to directly text compare with Class Names (UObjectBase::GetFName), which is easier but potentially
*								 slower
*								 
* Sample Usage:
*		SAssignNew(MyFilterBar, SFilterBar<FText>)
*		.OnFilterChanged() // A delegate for when the list of filters changes
*		.CustomFilters() // An array of filters available to this FilterBar (@see FGenericFilter to create simple delegate based filters)
*		.OnConvertItemToAssetData() // Conversion Function as mentioned above
*
* Use the GetAllActiveFilters() function to get the FilterCollection of Active Filters in this FilterBar, that can be used to filter your items.
* NOTE: GetAllActiveFilters() must be called every time the filters change (in OnFilterChanged() for example) to make sure you have the correct backend filter
* NOTE: Use CustomClassFilters to provide any Type Filters to make sure they get resolved properly (See FCustomClassFilterData)
* NOTE: Use MakeAddFilterButton() to make the button that summons the dropdown showing all the filters
*/
template<typename FilterType>
class SFilterBar : public SAssetFilterBar<FilterType>, public FFilterBarBase
{
public:
	using FOnFilterChanged = typename SBasicFilterBar<FilterType>::FOnFilterChanged;
	using FConvertItemToAssetData = typename FAssetFilter<FilterType>::FConvertItemToAssetData;
	using FCompareItemWithClassNames = typename FAssetFilter<FilterType>::FCompareItemWithClassNames;
	using FOnExtendAddFilterMenu = typename SBasicFilterBar<FilterType>::FOnExtendAddFilterMenu;
	using FCreateTextFilter = typename SBasicFilterBar<FilterType>::FCreateTextFilter;
	using SFilter = typename SBasicFilterBar<FilterType>::SFilter;
	
	SLATE_BEGIN_ARGS( SFilterBar<FilterType> )
		: _FilterBarIdentifier(NAME_None)
		, _UseDefaultAssetFilters(true)
		{
		
		}

		/** Delegate for when filters have changed */
		SLATE_EVENT( FOnFilterChanged, OnFilterChanged )

		/** Specify this delegate to use asset comparison through IAssetRegistry, where you specify how to convert your Item into an FAssetData */
		SLATE_EVENT( FConvertItemToAssetData, OnConvertItemToAssetData )

		/** Specify this delegate to use simple asset comparison, where you compare your Item a list of FNames representing Classes */
		SLATE_EVENT( FCompareItemWithClassNames, OnCompareItemWithClassNames )

		/** Initial List of Custom Filters that will be added to the AddFilter Menu */
		SLATE_ARGUMENT( TArray<TSharedRef<FFilterBase<FilterType>>>, CustomFilters)

		/** A unique identifier for this filter bar needed to enable saving settings in a config file */
		SLATE_ARGUMENT(FName, FilterBarIdentifier)
	
		/** Delegate to extend the Add Filter dropdown */
		SLATE_EVENT( FOnExtendAddFilterMenu, OnExtendAddFilterMenu )

		/** Initial List of Custom Class Filters that will be added to the AddFilter Menu */
		SLATE_ARGUMENT( TArray<TSharedRef<FCustomClassFilterData>>, CustomClassFilters)
	
		/** Whether the filter bar should provide the default asset filters */
		SLATE_ARGUMENT(bool, UseDefaultAssetFilters)

		/** A delegate to create a TTextFilter for FilterType items. If provided, will allow creation of custom text filters
		 *  from the filter dropdown menu.
		 */
		SLATE_ARGUMENT(FCreateTextFilter, CreateTextFilter)
			
		/** An SFilterSearchBox that can be attached to this filter bar. When provided along with a CreateTextFilter
		 *  delegate, allows the user to save searches from the Search Box as text filters for the filter bar.
		 *	NOTE: Will bind a delegate to SFilterSearchBox::OnClickedAddSearchHistoryButton
		 */
		SLATE_ARGUMENT(TSharedPtr<SFilterSearchBox>, FilterSearchBox)
	
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct( const FArguments& InArgs )
	{
		typename SAssetFilterBar<FilterType>::FArguments Args;
		Args._OnFilterChanged = InArgs._OnFilterChanged;
		Args._CustomFilters = InArgs._CustomFilters;
		Args._UseDefaultAssetFilters = InArgs._UseDefaultAssetFilters;
		Args._CustomClassFilters = InArgs._CustomClassFilters;
		Args._OnExtendAddFilterMenu = InArgs._OnExtendAddFilterMenu;
		Args._CreateTextFilter = InArgs._CreateTextFilter;
		Args._FilterSearchBox = InArgs._FilterSearchBox;
		
		SAssetFilterBar<FilterType>::Construct(Args);

		FilterBarIdentifier = InArgs._FilterBarIdentifier;
		
		// Create the dummy filter that represents all currently active Asset Type filters
		AssetFilter = MakeShareable(new FAssetFilter<FilterType>);

		// Asset Conversion is preferred to Asset Comparison
		if(InArgs._OnConvertItemToAssetData.IsBound())
		{
			AssetFilter->SetConversionFunction(InArgs._OnConvertItemToAssetData);
		}
		else if(InArgs._OnCompareItemWithClassNames.IsBound())
		{
			AssetFilter->SetComparisonFunction(InArgs._OnCompareItemWithClassNames);
		}
		else
		{
			UE_LOG(LogSlate, Error, TEXT("SFilterBar Requires that you specify either OnConvertItemToAssetData or OnCompareItemWithClassName"));
		}

		this->ActiveFilters->Add(AssetFilter);

		InitializeConfig();
	}
	
	/** Use this function to get all currently active filters (to filter your items)
	 * NOTE: Must be called every time the filters change (OnFilterChanged) to make sure you get the correct combined filter
	 */
	virtual TSharedPtr< TFilterCollection<FilterType> > GetAllActiveFilters() override
	{
		UpdateAssetFilter();
		
		return this->ActiveFilters;	
	}

	virtual void SaveSettings()
	{
		FFilterBarSettings* FilterBarConfig = GetMutableConfig();
		
		if(!FilterBarConfig)
		{
			UE_LOG(LogSlate, Error, TEXT("SFilterBar Requires that you specify a FilterBarIdentifier to save settings"));
			return;
		}

		// Empty the config, we are just going to re-save everything
		FilterBarConfig->Empty();

		/* Go through all the custom text filters (including unchecked ones) to save them, so that the user does not
		 * lose the text filters they created from the menu or a saved search
		 */
		for (const TSharedRef<FCustomTextFilter<FilterType>>& CustomTextFilter : this->CustomTextFilters)
		{
			// Is the filter "checked", i.e visible in the filter bar
			bool bIsChecked = this->IsFrontendFilterInUse(CustomTextFilter);

			// Is the filter "active", i.e visible and enabled in the filter bar
			bool bIsActive = this->IsFilterActive(CustomTextFilter);

			// Get the data associated with this filter
			FCustomTextFilterData FilterData = CustomTextFilter->CreateCustomTextFilterData();

			FCustomTextFilterState FilterState;
			FilterState.bIsChecked = bIsChecked;
			FilterState.bIsActive = bIsActive;
			FilterState.FilterData = FilterData;

			FilterBarConfig->CustomTextFilters.Add(FilterState);
		}
		
		/* For the remaining (custom and type) filters, go through the currently active (i.e visible in the filter bar)
		 * ones to save their state, since they will be added to the filter bar programatically every time
		 */
		for ( const TSharedPtr<SFilter> Filter : this->Filters )
		{
			const FString FilterName = Filter->GetFilterName();

			// Ignore custom text filters, since we saved them previously
			if(FilterName == FCustomTextFilter<FilterType>::GetCommonName().ToString())
			{
				
			}
			// If it is a FrontendFilter
			else if ( Filter->GetFrontendFilter().IsValid() )
			{
				FilterBarConfig->CustomFilters.Add(FilterName, Filter->IsEnabled());
			}
			// Otherwise we assume it is a type filter
			else
			{
				FilterBarConfig->TypeFilters.Add(FilterName, Filter->IsEnabled());
			}
		}

		SaveConfig();
	}

	virtual void LoadSettings()
	{
		const FFilterBarSettings* FilterBarConfig = GetConstConfig();
		
		if(!FilterBarConfig)
		{
			UE_LOG(LogSlate, Error, TEXT("SFilterBar Requires that you specify a FilterBarIdentifier to load settings"));
			return;
		}

		if(!FilterBarConfig->CustomTextFilters.IsEmpty())
		{
			// We must have a CreateTextFilter bound if we have any custom text filters saved!!
			check(this->CreateTextFilter.IsBound());
		}

		// Load all the custom filters (i.e FrontendFilters)
		for ( auto FrontendFilterIt = this->AllFrontendFilters.CreateIterator(); FrontendFilterIt; ++FrontendFilterIt )
		{
			TSharedRef<FFilterBase<FilterType>>& FrontendFilter = *FrontendFilterIt;
			const FString& FilterName = FrontendFilter->GetName();
			
			if (!this->IsFrontendFilterInUse(FrontendFilter))
			{
				// Try to find this type filter in our list of saved filters
				if ( const bool* bIsActive = FilterBarConfig->CustomFilters.Find(FilterName) )
				{
					TSharedRef<SFilter> NewFilter = this->AddFilterToBar(FrontendFilter);
					
					NewFilter->SetEnabled(*bIsActive, false);
					this->SetFrontendFilterActive(FrontendFilter, NewFilter->IsEnabled());
				}
			}
		}
		
		// Load all the type filters
		for(const TSharedRef<FCustomClassFilterData> &CustomClassFilter : this->CustomClassFilters)
		{
			if(!this->IsClassTypeInUse(CustomClassFilter))
			{
				const FString FilterName = CustomClassFilter->GetFilterName();

				// Try to find this type filter in our list of saved filters
				if ( const bool* bIsActive = FilterBarConfig->TypeFilters.Find(CustomClassFilter->GetFilterName()) )
				{
					TSharedRef<SFilter> NewFilter = this->AddAssetFilterToBar(CustomClassFilter);
					NewFilter->SetEnabled(*bIsActive, false);
				}
			}
		}

		// Load all the custom text filters
		for(const FCustomTextFilterState& FilterState : FilterBarConfig->CustomTextFilters)
		{
			// Create a TTextFilter for the current filter we are making using the provided delegate
			TSharedRef<TTextFilter<FilterType>> NewTextFilter = this->CreateTextFilter.Execute().ToSharedRef();

			// Create the actual text filter
			TSharedRef<FCustomTextFilter<FilterType>> NewFilter = MakeShared<FCustomTextFilter<FilterType>>(NewTextFilter);

			// Set the internals of the custom text filter from what we have saved
			NewFilter->SetFromCustomTextFilterData(FilterState.FilterData);

			// Add this to our list of custom text filters
			this->CustomTextFilters.Add(NewFilter);

			// If the filter was checked previously, add it to the filter bar
			if(FilterState.bIsChecked)
			{
				TSharedRef<SFilter> AddedFilter = this->AddFilterToBar(NewFilter);

				// Set the filter as active if it was previously
				AddedFilter->SetEnabled(FilterState.bIsActive, false);
				
				this->SetFrontendFilterActive(NewFilter, FilterState.bIsActive);
			}
		}

		this->OnFilterChanged.ExecuteIfBound();
	}

private:

	void UpdateAssetFilter()
	{
		/** We have to make sure to update the CombinedBackendFilter everytime the user requests all the Filters */
		FARFilter CombinedBackendFilter = this->GetCombinedBackendFilter();
		AssetFilter->SetBackendFilter(CombinedBackendFilter);
	}
	
private:
	/* The invisible filter used to conduct Asset Type filtering */
	TSharedPtr<FAssetFilter<FilterType>> AssetFilter;
};
