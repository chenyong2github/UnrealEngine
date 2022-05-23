// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Filters/FilterBase.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Textures/SlateIcon.h"
#include "Widgets/Layout/SBorder.h"
#include "ToolMenus.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Misc/FilterCollection.h"
#include "UObject/Object.h"

#include "SBasicFilterBar.generated.h"

#define LOCTEXT_NAMESPACE "FilterBar"

/* Delegate used by SBasicFilterBar to populate the add filter menu */
DECLARE_DELEGATE_OneParam(FOnPopulateAddFilterMenu, UToolMenu*)

/** ToolMenuContext that is used to create the Add Filter Menu */
UCLASS()
class TOOLWIDGETS_API UFilterBarContext : public UObject
{
	GENERATED_BODY()

public:
	FOnPopulateAddFilterMenu PopulateFilterMenu;
};

/** Forward declaration for a helper struct used to activate external filters */
template<typename FilterType>
struct FFrontendFilterExternalActivationHelper;

/**
* A Basic Filter Bar widget, which can be used to filter items of type [FilterType] given a list of custom filters
* @see SFilterBar in EditorWidgets if you want a Filter Bar that includes Asset Type Filters
* NOTE: The filter functions create copies, so you want to use a reference or pointer as the template type when possible
* Sample Usage:
*		SAssignNew(MyFilterBar, SBasicFilterBar<FText&>)
*		.OnFilterChanged() // A delegate for when the list of filters changes
*		.CustomFilters() // An array of filters available to this FilterBar (@see FGenericFilter to create simple delegate based filters)
*
* Use the GetAllActiveFilters() function to get the FilterCollection of Active Filters in this FilterBar, that can be used to filter your items
* Use MakeAddFilterButton() to make the button that summons the dropdown showing all the filters
*/
template<typename FilterType>
class SBasicFilterBar : public SCompoundWidget
{
public:
	/** Delegate for when filters have changed */
	DECLARE_DELEGATE( FOnFilterChanged );
	
 	SLATE_BEGIN_ARGS( SBasicFilterBar<FilterType> ){}

 		/** Delegate for when filters have changed */
 		SLATE_EVENT( FOnFilterChanged, OnFilterChanged )

		/** Initial List of Custom Filters that will be added to the AddFilter Menu */
		SLATE_ARGUMENT( TArray<TSharedRef<FFilterBase<FilterType>>>, CustomFilters)
	
 	SLATE_END_ARGS()

 	void Construct( const FArguments& InArgs )
 	{
		OnFilterChanged = InArgs._OnFilterChanged;

 		// A subclass could be using an external filter collection (SFilterList in ContentBrowser)
 		if(!ActiveFilters)
 		{
 			ActiveFilters = MakeShareable(new TFilterCollection<FilterType>());
 		}

 		// Add the custom filters
 		for (TSharedRef<FFilterBase<FilterType>> Filter : InArgs._CustomFilters)
 		{
 			AddFilter(Filter);
 		}
 	}

	/** Add a custom filter to the FilterBar.
	 * NOTE: This only adds it to the Add Filter Menu, and does not automatically add it to the filter bar or activate it
	 */
	void AddFilter(TSharedRef<FFilterBase<FilterType>> InFilter)
 	{
		AllFrontendFilters.Add(InFilter);
 		
 		// Add the category for the filter
 		if (TSharedPtr<FFilterCategory> Category = InFilter->GetCategory())
 		{
 			AllFilterCategories.AddUnique(Category);
 		}
 			
 		// Bind external activation event
 		FFrontendFilterExternalActivationHelper<FilterType>::BindToFilter(SharedThis(this), InFilter);

 		// Auto add if it is an inverse filters
 		SetFrontendFilterActive(InFilter, false);
 	}

	/** Makes the button that summons the Add Filter dropdown on click */
	static TSharedRef<SWidget> MakeAddFilterButton(TSharedRef<SBasicFilterBar<FilterType>> InFilterBar)
 	{
 		return SNew(SComboButton)
				.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButtonWithIcon"))
				.ForegroundColor(FSlateColor::UseStyle())
				.ToolTipText(LOCTEXT("AddFilterToolTip", "Open the Add Filter Menu to add or manage filters."))
				.OnGetMenuContent(InFilterBar, &SBasicFilterBar<FilterType>::MakeAddFilterMenu)
				.ContentPadding(FMargin(1, 0))
				.ButtonContent()
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Filter"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				];
 	}

private:

	/** A class for check boxes in the filter list. If you double click a filter checkbox, you will enable it and disable all others */
	class SFilterCheckBox : public SCheckBox
	{
	public:
		
		void SetOnFilterCtrlClicked(const FOnClicked& NewFilterCtrlClicked)
		{
			OnFilterCtrlClicked = NewFilterCtrlClicked;
		}

		void SetOnFilterAltClicked(const FOnClicked& NewFilteAltClicked)
		{
			OnFilterAltClicked = NewFilteAltClicked;
		}

		void SetOnFilterDoubleClicked( const FOnClicked& NewFilterDoubleClicked )
		{
			OnFilterDoubleClicked = NewFilterDoubleClicked;
		}

		void SetOnFilterMiddleButtonClicked( const FOnClicked& NewFilterMiddleButtonClicked )
		{
			OnFilterMiddleButtonClicked = NewFilterMiddleButtonClicked;
		}

		virtual FReply OnMouseButtonDoubleClick( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent ) override
		{
			if ( InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OnFilterDoubleClicked.IsBound() )
			{
				return OnFilterDoubleClicked.Execute();
			}
			else
			{
				return SCheckBox::OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent);
			}
		}

		virtual FReply OnMouseButtonUp( const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent ) override
		{
			if (InMouseEvent.IsControlDown() && OnFilterCtrlClicked.IsBound())
			{
				return OnFilterCtrlClicked.Execute();
			}
			else if (InMouseEvent.IsAltDown() && OnFilterAltClicked.IsBound())
			{
				return OnFilterAltClicked.Execute();
			}
			else if( InMouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton && OnFilterMiddleButtonClicked.IsBound() )
			{
				return OnFilterMiddleButtonClicked.Execute();
			}
			else
			{
				SCheckBox::OnMouseButtonUp(InMyGeometry, InMouseEvent);
				return FReply::Handled().ReleaseMouseCapture();
			}
		}

	private:
		FOnClicked OnFilterCtrlClicked;
		FOnClicked OnFilterAltClicked;
		FOnClicked OnFilterDoubleClicked;
		FOnClicked OnFilterMiddleButtonClicked;
	};

protected:

	/**
	 * A single filter in the filter list. Can be removed by clicking the remove button on it.
	 */
	class SFilter : public SCompoundWidget
	{
	public:
		DECLARE_DELEGATE_OneParam( FOnRequestRemove, const TSharedRef<SFilter>& /*FilterToRemove*/ );
		DECLARE_DELEGATE_OneParam( FOnRequestRemoveAllButThis, const TSharedRef<SFilter>& /*FilterToKeep*/ );
		DECLARE_DELEGATE_OneParam( FOnRequestEnableOnly, const TSharedRef<SFilter>& /*FilterToEnable*/ );
		DECLARE_DELEGATE( FOnRequestEnableAll );
		DECLARE_DELEGATE( FOnRequestDisableAll );
		DECLARE_DELEGATE( FOnRequestRemoveAll );

		SLATE_BEGIN_ARGS( SFilter ){}

			/** If this is an front end filter, this is the filter object */
			SLATE_ARGUMENT( TSharedPtr<FFilterBase<FilterType>>, FrontendFilter )

			/** Invoked when the filter toggled */
			SLATE_EVENT( SBasicFilterBar<FilterType>::FOnFilterChanged, OnFilterChanged )

			/** Invoked when a request to remove this filter originated from within this filter */
			SLATE_EVENT( FOnRequestRemove, OnRequestRemove )

			/** Invoked when a request to enable only this filter originated from within this filter */
			SLATE_EVENT( FOnRequestEnableOnly, OnRequestEnableOnly )

			/** Invoked when a request to enable all filters originated from within this filter */
			SLATE_EVENT(FOnRequestEnableAll, OnRequestEnableAll)

			/** Invoked when a request to disable all filters originated from within this filter */
			SLATE_EVENT( FOnRequestDisableAll, OnRequestDisableAll )

			/** Invoked when a request to remove all filters originated from within this filter */
			SLATE_EVENT( FOnRequestRemoveAll, OnRequestRemoveAll )

			/** Invoked when a request to remove all filters originated from within this filter */
			SLATE_EVENT( FOnRequestRemoveAllButThis, OnRequestRemoveAllButThis )

		SLATE_END_ARGS()

		/** Constructs this widget with InArgs */
		void Construct( const FArguments& InArgs )
		{
			bEnabled = false;
			OnFilterChanged = InArgs._OnFilterChanged;
			OnRequestRemove = InArgs._OnRequestRemove;
			OnRequestEnableOnly = InArgs._OnRequestEnableOnly;
			OnRequestEnableAll = InArgs._OnRequestEnableAll;
			OnRequestDisableAll = InArgs._OnRequestDisableAll;
			OnRequestRemoveAll = InArgs._OnRequestRemoveAll;
			OnRequestRemoveAllButThis = InArgs._OnRequestRemoveAllButThis;
			FrontendFilter = InArgs._FrontendFilter;

			// Get the tooltip and color of the type represented by this filter
			FilterColor = FLinearColor::White;
			if ( FrontendFilter.IsValid() )
			{
				FilterColor = FrontendFilter->GetColor();
				FilterToolTip = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(FrontendFilter.ToSharedRef(), &FFilterBase<FilterType>::GetToolTipText));
			}

			Construct_Internal();
		}

		/** Sets whether or not this filter is applied to the combined filter */
		void SetEnabled(bool InEnabled, bool InExecuteOnFilterChanged = true)
		{
			if ( InEnabled != bEnabled)
			{
				bEnabled = InEnabled;
				if (InExecuteOnFilterChanged)
				{
					OnFilterChanged.ExecuteIfBound();
				}
			}
		}

		/** Returns true if this filter contributes to the combined filter */
		bool IsEnabled() const
		{
			return bEnabled;
		}
		

		/** If this is an front end filter, this is the filter object */
		const TSharedPtr<FFilterBase<FilterType>>& GetFrontendFilter() const
		{
			return FrontendFilter;
		}

		/** Returns the display name for this filter */
		virtual FText GetFilterDisplayName() const
		{
			FText FilterName;
			if (FrontendFilter.IsValid())
			{
				FilterName = FrontendFilter->GetDisplayName();
			}

			if (FilterName.IsEmpty())
			{
				FilterName = LOCTEXT("UnknownFilter", "???");
			}

			return FilterName;
		}
		
		virtual FString GetFilterName() const
		{
			FString FilterName;
			if (FrontendFilter.IsValid())
			{
				FilterName = FrontendFilter->GetName();
			}
			return FilterName;
		}

	protected:

		/** Function that constructs the actual widget for subclasses to call */
		void Construct_Internal()
		{
			ChildSlot
			[
				SNew(SBorder)
				 .Padding(1.0f)
				.BorderImage(FAppStyle::Get().GetBrush("ContentBrowser.FilterBackground"))
				 [
					SAssignNew( ToggleButtonPtr, SFilterCheckBox )
					.Style(FAppStyle::Get(), "ContentBrowser.FilterButton")
					.ToolTipText(FilterToolTip)
					.Padding(0.0f)
					.IsChecked(this, &SFilter::IsChecked)
					.OnCheckStateChanged(this, &SFilter::FilterToggled)
					.OnGetMenuContent(this, &SFilter::GetRightClickMenuContent)
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.AutoWidth()
						[
							SNew(SImage)
							.Image(FAppStyle::Get().GetBrush("ContentBrowser.FilterImage"))
							.ColorAndOpacity(this, &SFilter::GetFilterImageColorAndOpacity)
						]
						+SHorizontalBox::Slot()
						.Padding(TAttribute<FMargin>(this, &SFilter::GetFilterNamePadding))
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(this, &SFilter::GetFilterDisplayName)
							.IsEnabled_Lambda([this] {return bEnabled;})
						]
					]
				]
			];

			ToggleButtonPtr->SetOnFilterCtrlClicked(FOnClicked::CreateSP(this, &SFilter::FilterCtrlClicked));
			ToggleButtonPtr->SetOnFilterAltClicked(FOnClicked::CreateSP(this, &SFilter::FilterAltClicked));
			ToggleButtonPtr->SetOnFilterDoubleClicked( FOnClicked::CreateSP(this, &SFilter::FilterDoubleClicked) );
			ToggleButtonPtr->SetOnFilterMiddleButtonClicked( FOnClicked::CreateSP(this, &SFilter::FilterMiddleButtonClicked) );
		}
		
		/** Handler for when the filter checkbox is clicked */
		void FilterToggled(ECheckBoxState NewState)
		{
			bEnabled = NewState == ECheckBoxState::Checked;
			OnFilterChanged.ExecuteIfBound();
		}

		/** Handler for when the filter checkbox is clicked and a control key is pressed */
		FReply FilterCtrlClicked()
		{
			OnRequestEnableAll.ExecuteIfBound();
			return FReply::Handled();
		}

		/** Handler for when the filter checkbox is clicked and an alt key is pressed */
		FReply FilterAltClicked()
		{
			OnRequestDisableAll.ExecuteIfBound();
			return FReply::Handled();
		}

		/** Handler for when the filter checkbox is double clicked */
		FReply FilterDoubleClicked()
		{
			// Disable all other filters and enable this one.
			OnRequestDisableAll.ExecuteIfBound();
			bEnabled = true;
			OnFilterChanged.ExecuteIfBound();

			return FReply::Handled();
		}

		/** Handler for when the filter checkbox is middle button clicked */
		FReply FilterMiddleButtonClicked()
		{
			RemoveFilter();
			return FReply::Handled();
		}

		/** Handler to create a right click menu */
		TSharedRef<SWidget> GetRightClickMenuContent()
		{
			FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, NULL);

			MenuBuilder.BeginSection("FilterOptions", LOCTEXT("FilterContextHeading", "Filter Options"));
			{
				MenuBuilder.AddMenuEntry(
					FText::Format( LOCTEXT("RemoveFilter", "Remove: {0}"), GetFilterDisplayName() ),
					LOCTEXT("RemoveFilterTooltip", "Remove this filter from the list. It can be added again in the filters menu."),
					FSlateIcon(),
					FUIAction( FExecuteAction::CreateSP(this, &SFilter::RemoveFilter) )
					);

				MenuBuilder.AddMenuEntry(
					FText::Format( LOCTEXT("EnableOnlyThisFilter", "Enable Only This: {0}"), GetFilterDisplayName() ),
					LOCTEXT("EnableOnlyThisFilterTooltip", "Enable only this filter from the list."),
					FSlateIcon(),
					FUIAction( FExecuteAction::CreateSP(this, &SFilter::EnableOnly) )
					);

			}
			MenuBuilder.EndSection();

			MenuBuilder.BeginSection("FilterBulkOptions", LOCTEXT("BulkFilterContextHeading", "Bulk Filter Options"));
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("EnableAllFilters", "Enable All Filters"),
					LOCTEXT("EnableAllFiltersTooltip", "Enables all filters."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &SFilter::EnableAllFilters))
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("DisableAllFilters", "Disable All Filters"),
					LOCTEXT("DisableAllFiltersTooltip", "Disables all active filters."),
					FSlateIcon(),
					FUIAction( FExecuteAction::CreateSP(this, &SFilter::DisableAllFilters) )
					);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("RemoveAllFilters", "Remove All Filters"),
					LOCTEXT("RemoveAllFiltersTooltip", "Removes all filters from the list."),
					FSlateIcon(),
					FUIAction( FExecuteAction::CreateSP(this, &SFilter::RemoveAllFilters) )
					);

				MenuBuilder.AddMenuEntry(
					FText::Format( LOCTEXT("RemoveAllButThisFilter", "Remove All But This: {0}"), GetFilterDisplayName() ),
					LOCTEXT("RemoveAllButThisFilterTooltip", "Remove all other filters except this one from the list."),
					FSlateIcon(),
					FUIAction( FExecuteAction::CreateSP(this, &SFilter::RemoveAllButThis) )
					);
			}
			MenuBuilder.EndSection();

			if (FrontendFilter.IsValid())
			{
				FrontendFilter->ModifyContextMenu(MenuBuilder);
			}

			return MenuBuilder.MakeWidget();
		}

		/** Removes this filter from the filter list */
		void RemoveFilter()
		{
			TSharedRef<SFilter> Self = SharedThis(this);
			OnRequestRemove.ExecuteIfBound( Self );
		}

		/** Remove all but this filter from the filter list. */
		void RemoveAllButThis()
		{
			TSharedRef<SFilter> Self = SharedThis(this);
			OnRequestRemoveAllButThis.ExecuteIfBound(Self);
		}

		/** Enables only this filter from the filter list */
		void EnableOnly()
		{
			TSharedRef<SFilter> Self = SharedThis(this);
			OnRequestEnableOnly.ExecuteIfBound( Self );
		}

		/** Enables all filters in the list */
		void EnableAllFilters()
		{
			OnRequestEnableAll.ExecuteIfBound();
		}

		/** Disables all active filters in the list */
		void DisableAllFilters()
		{
			OnRequestDisableAll.ExecuteIfBound();
		}

		/** Removes all filters in the list */
		void RemoveAllFilters()
		{
			OnRequestRemoveAll.ExecuteIfBound();
		}

		/** Handler to determine the "checked" state of the filter checkbox */
		ECheckBoxState IsChecked() const
		{
			return bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}

		/** Handler to determine the color of the checkbox when it is checked */
		FSlateColor GetFilterImageColorAndOpacity() const
		{
			return bEnabled ? FilterColor : FAppStyle::Get().GetSlateColor("Colors.Recessed");
		}

		EVisibility GetFilterOverlayVisibility() const
		{
			return bEnabled ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
		}

		/** Handler to determine the padding of the checkbox text when it is pressed */
		FMargin GetFilterNamePadding() const
		{
			return ToggleButtonPtr->IsPressed() ? FMargin(4,2,4,0) : FMargin(4,1,4,1);
		}

	protected:
		/** Invoked when the filter toggled */
		FOnFilterChanged OnFilterChanged;

		/** Invoked when a request to remove this filter originated from within this filter */
		FOnRequestRemove OnRequestRemove;

		/** Invoked when a request to enable only this filter originated from within this filter */
		FOnRequestEnableOnly OnRequestEnableOnly;

		/** Invoked when a request to enable all filters originated from within this filter */
		FOnRequestEnableAll OnRequestEnableAll;

		/** Invoked when a request to disable all filters originated from within this filter */
		FOnRequestDisableAll OnRequestDisableAll;

		/** Invoked when a request to remove all filters originated from within this filter */
		FOnRequestDisableAll OnRequestRemoveAll;

		/** Invoked when a request to remove all filters except this one originated from within this filter */
		FOnRequestRemoveAllButThis OnRequestRemoveAllButThis;

		/** true when this filter should be applied to the search */
		bool bEnabled;

		/** If this is an front end filter, this is the filter object */
		TSharedPtr<FFilterBase<FilterType>> FrontendFilter;

		/** The button to toggle the filter on or off */
		TSharedPtr<SFilterCheckBox> ToggleButtonPtr;

		/** The color of the checkbox for this filter */
		FLinearColor FilterColor;

		/** The tooltip for this filter */
		TAttribute<FText> FilterToolTip;
	};

public:

 	/** Returns true if any filters are applied */
 	bool HasAnyFilters() const
 	{
 		return Filters.Num() > 0;
 	}

 	/** Retrieve a specific filter */
 	TSharedPtr<FFilterBase<FilterType>> GetFilter(const FString& InName) const
 	{
 		for (const TSharedRef<FFilterBase<FilterType>>& Filter : AllFrontendFilters)
 		{
 			if (Filter->GetName() == InName)
 			{
 				return Filter;
 			}
 		}
 		return TSharedPtr<FFilterBase<FilterType>>();
 	}

	/** Use this function to get all currently active filters (to filter your items)
	 * Not const on purpose: Subclasses might need to update the filter collection before getting it
	 */
	virtual TSharedPtr< TFilterCollection<FilterType> > GetAllActiveFilters()
 	{
 		return ActiveFilters;	
 	}

	/** Enable all the filters that are currently visible on the filter bar */
	void EnableAllFilters()
 	{
 		for (const TSharedRef<SFilter>& Filter : Filters)
 		{
 			Filter->SetEnabled(true, false);
 			if (const TSharedPtr<FFilterBase<FilterType>>& FrontendFilter = Filter->GetFrontendFilter())
 			{
 				SetFrontendFilterActive(FrontendFilter.ToSharedRef(), true);
 			}
 		}

 		OnFilterChanged.ExecuteIfBound();
 	}

	/** Disable all the filters that are currently visible on the filter bar */
	void DisableAllFilters()
 	{
 		for (const TSharedRef<SFilter>& Filter : Filters)
 		{
 			Filter->SetEnabled(false, false);
 			if (const TSharedPtr<FFilterBase<FilterType>>& FrontendFilter = Filter->GetFrontendFilter())
 			{
 				SetFrontendFilterActive(FrontendFilter.ToSharedRef(), false);
 			}
 		}

 		OnFilterChanged.ExecuteIfBound();
 	}

	/** Remove all filters from the filter bar, while disabling any active ones */
	virtual void RemoveAllFilters()
 	{
 		if (HasAnyFilters())
 		{
 			// Update the frontend filters collection
 			for (const TSharedRef<SFilter>& FilterToRemove : Filters)
 			{
 				if (const TSharedPtr<FFilterBase<FilterType>>& FrontendFilter = FilterToRemove->GetFrontendFilter())
 				{
 					SetFrontendFilterActive(FrontendFilter.ToSharedRef(), false); // Deactivate.
 				}
 			}
		
 			ChildSlot
			 [
				 SNullWidget::NullWidget
			 ];

 			Filters.Empty();

 			// Notify that a filter has changed
 			OnFilterChanged.ExecuteIfBound();
 		}
 	}
	
 	/** Set the check box state of the specified filter (in the filter drop down) and pin/unpin a filter widget on/from the filter bar. When a filter is pinned (was not already pinned), it is activated and deactivated when unpinned. */
 	void SetFilterCheckState(const TSharedPtr<FFilterBase<FilterType>>& InFilter, ECheckBoxState InCheckState)
 	{
 		if (!InFilter || InCheckState == ECheckBoxState::Undetermined)
 		{
 			return;
 		}

 		// Check if the filter is already checked.
 		TSharedRef<FFilterBase<FilterType>> Filter = InFilter.ToSharedRef();
 		bool FrontendFilterChecked = IsFrontendFilterInUse(Filter);

 		if (InCheckState == ECheckBoxState::Checked && !FrontendFilterChecked)
 		{
 			AddFilterToBar(Filter)->SetEnabled(true); // Pin a filter widget on the UI and activate the filter. Same behaviour as FrontendFilterClicked()
 		}
 		else if (InCheckState == ECheckBoxState::Unchecked && FrontendFilterChecked)
 		{
 			RemoveFilter(Filter); // Unpin the filter widget and deactivate the filter.
 		}
 		// else -> Already in the desired 'check' state.
 	}

 	/** Returns the check box state of the specified filter (in the filter drop down). This tells whether the filter is pinned or not on the filter bar, but not if filter is active or not. @see IsFrontendFilterActive(). */
 	ECheckBoxState GetFilterCheckState(const TSharedPtr<FFilterBase<FilterType>>& InFilter) const
 	{
 		return InFilter && IsFrontendFilterInUse(InFilter.ToSharedRef()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
 	}

 	/** Returns true if the specified filter is both checked (pinned on the filter bar) and active (contributing to filter the result). */
 	bool IsFilterActive(const TSharedPtr<FFilterBase<FilterType>>& InFilter) const
 	{
 		if (InFilter.IsValid())
 		{
 			for (const TSharedRef<SFilter>& Filter : Filters)
 			{
 				if (InFilter == Filter->GetFrontendFilter())
 				{
 					return Filter->IsEnabled(); // Is active or not?
 				}
 			}
 		}
 		return false;
 	}

protected:
	
	/** Remove all filters except the specified one */
	virtual void RemoveAllButThis(const TSharedRef<SFilter>& FilterToKeep)
	{
		for (const TSharedRef<SFilter>& Filter : Filters)
		{
			if (Filter == FilterToKeep)
			{
				continue;
			}

			if (const TSharedPtr<FFilterBase<FilterType>>& FrontendFilter = Filter->GetFrontendFilter())
			{
				SetFrontendFilterActive(FrontendFilter.ToSharedRef(), false);
			}
		}

		Filters.Empty();

		AddFilterToBar(FilterToKeep);

		OnFilterChanged.ExecuteIfBound();
	}

	/** Helper that creates a toolbar with all the given SFilter's as toolbar items. Filters that don't fit appear in the overflow menu as toggles. */
	static TSharedRef<SWidget> MakeFilterToolBarWidget(const TArray<TSharedRef<SFilter>>& Filters)
	{
		FSlimHorizontalToolBarBuilder ToolbarBuilder(TSharedPtr<const FUICommandList>(), FMultiBoxCustomization::None, TSharedPtr<FExtender>(), true);
		ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);
		ToolbarBuilder.SetStyle(&FAppStyle::Get(), "ContentBrowser.FilterToolBar");

		for (const TSharedRef<SFilter>& Filter : Filters)
		{
			ToolbarBuilder.AddWidget(Filter, NAME_None, true, EHorizontalAlignment::HAlign_Fill, FNewMenuDelegate::CreateLambda([Filter](FMenuBuilder& MenuBuilder)
			{
				FUIAction Action;
				Action.GetActionCheckState = FGetActionCheckState::CreateLambda([Filter]()
				{
					return Filter->IsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				});
				Action.ExecuteAction = FExecuteAction::CreateLambda([Filter]()
				{
					Filter->SetEnabled(!Filter->IsEnabled());
				});

				MenuBuilder.AddMenuEntry(Filter->GetFilterDisplayName(), FText::GetEmpty(), FSlateIcon(), Action, NAME_None, EUserInterfaceActionType::ToggleButton);
			}));
		}

		return ToolbarBuilder.MakeWidget();
	}
	
 	/** Sets the active state of a frontend filter. */
 	void SetFrontendFilterActive(const TSharedRef<FFilterBase<FilterType>>& Filter, bool bActive)
	{
		if(Filter->IsInverseFilter())
		{
			//Inverse filters are active when they are "disabled"
			bActive = !bActive;
		}
		Filter->ActiveStateChanged(bActive);

		if ( bActive )
		{
			ActiveFilters->Add(Filter);
		}
		else
		{
			ActiveFilters->Remove(Filter);
		}
	}

	/* 'Activate' A filter by adding it to the filter bar, does not turn it on */
	TSharedRef<SFilter> AddFilterToBar(const TSharedRef<FFilterBase<FilterType>>& Filter)
	{
		TSharedRef<SFilter> NewFilter =
			SNew(SFilter)
			.FrontendFilter(Filter)
			.OnFilterChanged( this, &SBasicFilterBar<FilterType>::FrontendFilterChanged, Filter )
			.OnRequestRemove(this, &SBasicFilterBar<FilterType>::RemoveFilterAndUpdate)
			.OnRequestEnableOnly(this, &SBasicFilterBar<FilterType>::EnableOnlyThisFilter)
			.OnRequestEnableAll(this, &SBasicFilterBar<FilterType>::EnableAllFilters)
			.OnRequestDisableAll(this, &SBasicFilterBar<FilterType>::DisableAllFilters)
			.OnRequestRemoveAll(this, &SBasicFilterBar<FilterType>::RemoveAllFilters)
			.OnRequestRemoveAllButThis(this, &SBasicFilterBar<FilterType>::RemoveAllButThis);

		AddFilterToBar( NewFilter );

		return NewFilter;
	}

	/* 'Activate' A filter by adding it to the filter bar, does not turn it on */
	void AddFilterToBar(const TSharedRef<SFilter>& FilterToAdd)
	{
		Filters.Add(FilterToAdd);
	
		ChildSlot
		[
			MakeFilterToolBarWidget(Filters)
		];
	}

	/** Handler for when the enable only this button was clicked on a single filter */
	void EnableOnlyThisFilter(const TSharedRef<SFilter>& FilterToEnable)
	{
		for (const TSharedRef<SFilter>& Filter : Filters)
		{
			bool bEnable = Filter == FilterToEnable;
			Filter->SetEnabled(bEnable, /*ExecuteOnFilterChange*/false);
			if (const TSharedPtr<FFilterBase<FilterType>>& FrontendFilter = Filter->GetFrontendFilter())
			{
				SetFrontendFilterActive(FrontendFilter.ToSharedRef(), bEnable);
			}
		}

		OnFilterChanged.ExecuteIfBound();
	}

	/** Remove a filter from the filter bar */
	void RemoveFilter(const TSharedRef<FFilterBase<FilterType>>& InFilterToRemove, bool ExecuteOnFilterChanged= true)
	{
		TSharedPtr<SFilter> FilterToRemove;
		for (const TSharedRef<SFilter>& Filter : Filters)
		{
			if (Filter->GetFrontendFilter() == InFilterToRemove)
			{
				FilterToRemove = Filter;
				break;
			}
		}

		if (FilterToRemove.IsValid())
		{
			if (ExecuteOnFilterChanged)
			{
				RemoveFilterAndUpdate(FilterToRemove.ToSharedRef());
			}
			else
			{
				RemoveFilter(FilterToRemove.ToSharedRef());
			}
		}
	}

	/** Remove a filter from the filter bar */
	virtual void RemoveFilter(const TSharedRef<SFilter>& FilterToRemove)
	{
		Filters.Remove(FilterToRemove);

		if (const TSharedPtr<FFilterBase<FilterType>>& FrontendFilter = FilterToRemove->GetFrontendFilter()) // Is valid?
		{
			// Update the frontend filters collection
			SetFrontendFilterActive(FrontendFilter.ToSharedRef(), false);
			OnFilterChanged.ExecuteIfBound();
		}

		ChildSlot
		[
			MakeFilterToolBarWidget(Filters)
		];
	}

	/** Remove a filter from the filter bar */
	void RemoveFilterAndUpdate(const TSharedRef<SFilter>& FilterToRemove)
	{
		RemoveFilter(FilterToRemove);

		// Notify that a filter has changed
		OnFilterChanged.ExecuteIfBound();
	}

 	/** Handler for when a frontend filter state has changed */
 	void FrontendFilterChanged(TSharedRef<FFilterBase<FilterType>> FrontendFilter)
	{
		TSharedPtr<SFilter> FilterToUpdate;
		for (const TSharedRef<SFilter>& Filter : Filters)
		{
			if (Filter->GetFrontendFilter() == FrontendFilter)
			{
				FilterToUpdate = Filter;
				break;
			}
		}

		if (FilterToUpdate.IsValid())
		{
			SetFrontendFilterActive(FrontendFilter, FilterToUpdate->IsEnabled());
			OnFilterChanged.ExecuteIfBound();
		}
	}

 	/** Handler for when the add filter menu is populated by a non-category */
 	void CreateOtherFiltersMenuCategory(FToolMenuSection& Section, TSharedPtr<FFilterCategory> MenuCategory) const
	{
		for (const TSharedRef<FFilterBase<FilterType>>& FrontendFilter : AllFrontendFilters)
		{
			if(FrontendFilter->GetCategory() == MenuCategory)
			{
				Section.AddMenuEntry(
					NAME_None,
					FrontendFilter->GetDisplayName(),
					FrontendFilter->GetToolTipText(),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), FrontendFilter->GetIconName()),
					FUIAction(
					FExecuteAction::CreateSP(const_cast< SBasicFilterBar<FilterType>* >(this), &SBasicFilterBar<FilterType>::FrontendFilterClicked, FrontendFilter),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &SBasicFilterBar<FilterType>::IsFrontendFilterInUse, FrontendFilter)),
					EUserInterfaceActionType::ToggleButton
					);
			}
		}
	}

	/** Handler for when the add filter menu is populated by a non-category */
 	void CreateOtherFiltersMenuCategory(UToolMenu* InMenu, TSharedPtr<FFilterCategory> MenuCategory) const
	{
		CreateOtherFiltersMenuCategory(InMenu->AddSection("Section"), MenuCategory);
	}

	/** Handler for a frontend filter is clicked */
 	void FrontendFilterClicked(TSharedRef<FFilterBase<FilterType>> FrontendFilter)
	{
		if (IsFrontendFilterInUse(FrontendFilter))
		{
			RemoveFilter(FrontendFilter);
		}
		else
		{
			TSharedRef<SFilter> NewFilter = AddFilterToBar(FrontendFilter);
			NewFilter->SetEnabled(true);
		}
	}

	/** Handler to check if a frontend filter is in use */
 	bool IsFrontendFilterInUse(TSharedRef<FFilterBase<FilterType>> FrontendFilter) const
	{
		for (const TSharedRef<SFilter>& Filter : Filters)
		{
			if (Filter->GetFrontendFilter() == FrontendFilter)
			{
				return true;
			}
		}

		return false;
	}

	/** Handler for when a filter category is clicked */
 	void FrontendFilterCategoryClicked(TSharedPtr<FFilterCategory> MenuCategory)
	{
		bool bFullCategoryInUse = IsFrontendFilterCategoryInUse(MenuCategory);
		bool ExecuteOnFilterChanged = false;

		for (const TSharedRef<FFilterBase<FilterType>>& FrontendFilter : AllFrontendFilters)
		{
			if (FrontendFilter->GetCategory() == MenuCategory)
			{
				if (bFullCategoryInUse)
				{
					RemoveFilter(FrontendFilter, false);
					ExecuteOnFilterChanged = true;
				}
				else if (!IsFrontendFilterInUse(FrontendFilter))
				{
					TSharedRef<SFilter> NewFilter = AddFilterToBar(FrontendFilter);
					NewFilter->SetEnabled(true, false);
					SetFrontendFilterActive(FrontendFilter, NewFilter->IsEnabled());
					ExecuteOnFilterChanged = true;
				}
			}
		}

		if (ExecuteOnFilterChanged)
		{
			OnFilterChanged.ExecuteIfBound();
		}
	}
	
	/** Handler to check if a filter category is in use */
 	bool IsFrontendFilterCategoryInUse(TSharedPtr<FFilterCategory> MenuCategory) const
	{
		for (const TSharedRef<FFilterBase<FilterType>>& FrontendFilter : AllFrontendFilters)
		{
			if (FrontendFilter->GetCategory() == MenuCategory && !IsFrontendFilterInUse(FrontendFilter))
			{
				return false;
			}
		}

		return true;
	}
	
	/** Handler to determine the "checked" state of a frontend filter category in the filter dropdown */
    ECheckBoxState IsFrontendFilterCategoryChecked(TSharedPtr<FFilterCategory> MenuCategory) const
    {
    	bool bIsAnyActionInUse = false;
    	bool bIsAnyActionNotInUse = false;
		
		for (const TSharedRef<FFilterBase<FilterType>>& FrontendFilter : AllFrontendFilters)
		{
			if (FrontendFilter->GetCategory() == MenuCategory)
			{
				if (IsFrontendFilterInUse(FrontendFilter))
				{
					bIsAnyActionInUse = true;
				}
				else
				{
					bIsAnyActionNotInUse = true;
				}

				if (bIsAnyActionInUse && bIsAnyActionNotInUse)
				{
					return ECheckBoxState::Undetermined;
				}
			}
		}

    	if (bIsAnyActionInUse)
    	{
    		return ECheckBoxState::Checked;
    	}
    	else
    	{
    		return ECheckBoxState::Unchecked;
    	}
    }

 	/** Called when reset filters option is pressed */
 	void OnResetFilters()
	{
		RemoveAllFilters();
	}

 	/** Called to set a filter active externally */
 	void OnSetFilterActive(bool bInActive, TWeakPtr<FFilterBase<FilterType>> InWeakFilter)
 	{
 		TSharedPtr<FFilterBase<FilterType>> Filter = InWeakFilter.Pin();
 		if (Filter.IsValid())
 		{
 			if (!IsFrontendFilterInUse(Filter.ToSharedRef()))
 			{
 				TSharedRef<SFilter> NewFilter = AddFilterToBar(Filter.ToSharedRef());
 				NewFilter->SetEnabled(bInActive);
 			}
 		}
 	}

	/** Helper function to add common sections to the Add Filter Menu */
	void PopulateCommonFilterSections(UToolMenu* Menu)
	{
		FToolMenuSection& Section = Menu->AddSection("FilterBarResetFilters");
		Section.AddMenuEntry(
			"ResetFilters",
			LOCTEXT("FilterListResetFilters", "Reset Filters"),
			LOCTEXT("FilterListResetToolTip", "Resets current filter selection"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SBasicFilterBar<FilterType>::OnResetFilters))
			);
	}

	/** Helper function to add all custom filters to the Add Filter Menu */
	void PopulateCustomFilters(UToolMenu* Menu)
	{
		FToolMenuSection& Section = Menu->AddSection("BasicFilterBarFiltersMenu");
		
		// Add all the filters
		for (const TSharedPtr<FFilterCategory>& Category : AllFilterCategories)
		{
			Section.AddSubMenu(
				NAME_None,
				Category->Title,
				Category->Tooltip,
				FNewToolMenuDelegate::CreateSP(this, &SBasicFilterBar<FilterType>::CreateOtherFiltersMenuCategory, Category),
				FUIAction(
				FExecuteAction::CreateSP( this, &SBasicFilterBar<FilterType>::FrontendFilterCategoryClicked, Category ),
				FCanExecuteAction(),
				FGetActionCheckState::CreateSP(this, &SBasicFilterBar<FilterType>::IsFrontendFilterCategoryChecked, Category ) ),
				EUserInterfaceActionType::ToggleButton
				);
		}
	}
	
	/** Virtual Function for subclasses to save their settings */
	virtual void SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const {}

	/** Virtual Function for subclasses to load their settings */
	virtual void LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) {}

private:

	/** Handler for when the add filter button was clicked */
	virtual TSharedRef<SWidget> MakeAddFilterMenu()
	{
		const FName FilterMenuName = "FilterBar.FilterMenu";
		if (!UToolMenus::Get()->IsMenuRegistered(FilterMenuName))
		{
			UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(FilterMenuName);
			Menu->bShouldCloseWindowAfterMenuSelection = true;
			Menu->bCloseSelfOnly = true;

			Menu->AddDynamicSection(NAME_None, FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
			{
				if (UFilterBarContext* Context = InMenu->FindContext<UFilterBarContext>())
				{
					Context->PopulateFilterMenu.ExecuteIfBound(InMenu);
				}
			}));
		}

		UFilterBarContext* FilterBarContext = NewObject<UFilterBarContext>();
		FilterBarContext->PopulateFilterMenu = FOnPopulateAddFilterMenu::CreateSP(this, &SBasicFilterBar<FilterType>::PopulateAddFilterMenu);
		FToolMenuContext ToolMenuContext(FilterBarContext);

		return UToolMenus::Get()->GenerateWidget(FilterMenuName, ToolMenuContext);
	}

	/** Function to populate the add filter menu */
	void PopulateAddFilterMenu(UToolMenu* Menu)
	{
		PopulateCommonFilterSections(Menu);
		
		PopulateCustomFilters(Menu);
	}
	
protected:
 	/** All SFilters in the list */
 	TArray< TSharedRef<SFilter> > Filters;

 	/** All possible filter objects */
 	TArray< TSharedRef< FFilterBase<FilterType> > > AllFrontendFilters;

	/** Currently active filter objects */
	TSharedPtr< TFilterCollection<FilterType> > ActiveFilters;

 	/** All filter categories (for menu construction) */
 	TArray< TSharedPtr<FFilterCategory> > AllFilterCategories;

 	/** Delegate for when filters have changed */
 	FOnFilterChanged OnFilterChanged;

	friend struct FFrontendFilterExternalActivationHelper<FilterType>;
};

/** Helper struct to avoid friending the whole of SBasicFilterBarSBasicFilterBar */
template<typename FilterType>
struct FFrontendFilterExternalActivationHelper
{
	static void BindToFilter(TSharedRef<SBasicFilterBar<FilterType>> InFilterList, TSharedRef<FFilterBase<FilterType>> InFrontendFilter)
	{
		TWeakPtr<FFilterBase<FilterType>> WeakFilter = InFrontendFilter;
		InFrontendFilter->SetActiveEvent.AddSP(&InFilterList.Get(), &SBasicFilterBar<FilterType>::OnSetFilterActive, WeakFilter);
	}
};

#undef LOCTEXT_NAMESPACE