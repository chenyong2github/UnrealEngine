// Copyright Epic Games, Inc. All Rights Reserved.


#include "SFilterList.h"
#include "Styling/SlateTypes.h"
#include "Framework/Commands/UIAction.h"
#include "Textures/SlateIcon.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SBoxPanel.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/MenuStack.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SCheckBox.h"
#include "Styling/SlateTypes.h"
#include "EditorStyleSet.h"
#include "AssetRegistryModule.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "FrontendFilters.h"
#include "ContentBrowserFrontEndFilterExtension.h"
#include "Misc/BlacklistNames.h"

#define LOCTEXT_NAMESPACE "ContentBrowser"

/** Helper struct to avoid friending the whole of SFilterList */
struct FFrontendFilterExternalActivationHelper
{
	static void BindToFilter(TSharedRef<SFilterList> InFilterList, TSharedRef<FFrontendFilter> InFrontendFilter)
	{
		TWeakPtr<FFrontendFilter> WeakFilter = InFrontendFilter;
		InFrontendFilter->SetActiveEvent.AddSP(&InFilterList.Get(), &SFilterList::OnSetFilterActive, WeakFilter);
	}
};

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

/**
 * A single filter in the filter list. Can be removed by clicking the remove button on it.
 */
class SFilter : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam( FOnRequestRemove, const TSharedRef<SFilter>& /*FilterToRemove*/ );
	DECLARE_DELEGATE_OneParam( FOnRequestEnableOnly, const TSharedRef<SFilter>& /*FilterToEnable*/ );
	DECLARE_DELEGATE( FOnRequestEnableAll );
	DECLARE_DELEGATE( FOnRequestDisableAll );
	DECLARE_DELEGATE( FOnRequestRemoveAll );

	SLATE_BEGIN_ARGS( SFilter ){}

		/** The asset type actions that are associated with this filter */
		SLATE_ARGUMENT( TWeakPtr<IAssetTypeActions>, AssetTypeActions )

		/** If this is an front end filter, this is the filter object */
		SLATE_ARGUMENT( TSharedPtr<FFrontendFilter>, FrontendFilter )

		/** Invoked when the filter toggled */
		SLATE_EVENT( SFilterList::FOnFilterChanged, OnFilterChanged )

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

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct( const FArguments& InArgs )
	{
		bEnabled = false;
		OnFilterChanged = InArgs._OnFilterChanged;
		AssetTypeActions = InArgs._AssetTypeActions;
		OnRequestRemove = InArgs._OnRequestRemove;
		OnRequestEnableOnly = InArgs._OnRequestEnableOnly;
		OnRequestEnableAll = InArgs._OnRequestEnableAll;
		OnRequestDisableAll = InArgs._OnRequestDisableAll;
		OnRequestRemoveAll = InArgs._OnRequestRemoveAll;
		FrontendFilter = InArgs._FrontendFilter;

		// Get the tooltip and color of the type represented by this filter
		TAttribute<FText> FilterToolTip;
		FilterColor = FLinearColor::White;
		if ( InArgs._AssetTypeActions.IsValid() )
		{
			TSharedPtr<IAssetTypeActions> TypeActions = InArgs._AssetTypeActions.Pin();
			FilterColor = FLinearColor( TypeActions->GetTypeColor() );

			// No tooltip for asset type filters
		}
		else if ( FrontendFilter.IsValid() )
		{
			FilterColor = FrontendFilter->GetColor();
			FilterToolTip = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(FrontendFilter.ToSharedRef(), &FFrontendFilter::GetToolTipText));
		}

		ChildSlot
		[
			SNew(SBorder)
			.Padding(0)
			.BorderBackgroundColor( FLinearColor(0.2f, 0.2f, 0.2f, 0.2f) )
			.BorderImage(FEditorStyle::GetBrush("ContentBrowser.FilterButtonBorder"))
			[
				SAssignNew( ToggleButtonPtr, SFilterCheckBox )
				.Style(FEditorStyle::Get(), "ContentBrowser.FilterButton")
				.ToolTipText(FilterToolTip)
				.Padding(this, &SFilter::GetFilterNamePadding)
				.IsChecked(this, &SFilter::IsChecked)
				.OnCheckStateChanged(this, &SFilter::FilterToggled)
				.OnGetMenuContent(this, &SFilter::GetRightClickMenuContent)
				.ForegroundColor(this, &SFilter::GetFilterForegroundColor)
				[
					SNew(STextBlock)
					.ColorAndOpacity(this, &SFilter::GetFilterNameColorAndOpacity)
					.Font(FEditorStyle::GetFontStyle("ContentBrowser.FilterNameFont"))
					.ShadowOffset(FVector2D(1.f, 1.f))
					.Text(this, &SFilter::GetFilterName)
				]
			]
		];

		ToggleButtonPtr->SetOnFilterCtrlClicked(FOnClicked::CreateSP(this, &SFilter::FilterCtrlClicked));
		ToggleButtonPtr->SetOnFilterAltClicked(FOnClicked::CreateSP(this, &SFilter::FilterAltClicked));
		ToggleButtonPtr->SetOnFilterDoubleClicked( FOnClicked::CreateSP(this, &SFilter::FilterDoubleClicked) );
		ToggleButtonPtr->SetOnFilterMiddleButtonClicked( FOnClicked::CreateSP(this, &SFilter::FilterMiddleButtonClicked) );
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

	/** Returns this widgets contribution to the combined filter */
	FARFilter GetBackendFilter() const
	{
		FARFilter Filter;

		if ( AssetTypeActions.IsValid() )
		{
			if (AssetTypeActions.Pin()->CanFilter())
			{
				AssetTypeActions.Pin()->BuildBackendFilter(Filter);
			}
		}

		return Filter;
	}

	/** If this is an front end filter, this is the filter object */
	const TSharedPtr<FFrontendFilter>& GetFrontendFilter() const
	{
		return FrontendFilter;
	}

	/** Gets the asset type actions associated with this filter */
	const TWeakPtr<IAssetTypeActions>& GetAssetTypeActions() const
	{
		return AssetTypeActions;
	}

private:
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
				FText::Format( LOCTEXT("RemoveFilter", "Remove: {0}"), GetFilterName() ),
				LOCTEXT("RemoveFilterTooltip", "Remove this filter from the list. It can be added again in the filters menu."),
				FSlateIcon(),
				FUIAction( FExecuteAction::CreateSP(this, &SFilter::RemoveFilter) )
				);

			MenuBuilder.AddMenuEntry(
				FText::Format( LOCTEXT("EnableOnlyThisFilter", "Enable this only: {0}"), GetFilterName() ),
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
	FSlateColor GetFilterForegroundColor() const
	{
		return IsChecked() == ECheckBoxState::Checked ? FilterColor : FLinearColor::White;
	}

	/** Handler to determine the padding of the checkbox text when it is pressed */
	FMargin GetFilterNamePadding() const
	{
		return ToggleButtonPtr->IsPressed() ? FMargin(3,2,4,0) : FMargin(3,1,4,1);
	}

	/** Handler to determine the color of the checkbox text when it is hovered */
	FSlateColor GetFilterNameColorAndOpacity() const
	{
		const float DimFactor = 0.75f;
		return IsHovered() ? FLinearColor(DimFactor, DimFactor, DimFactor, 1.0f) : FLinearColor::White;
	}

	/** Returns the display name for this filter */
	FText GetFilterName() const
	{
		FText FilterName;
		if ( AssetTypeActions.IsValid() )
		{
			TSharedPtr<IAssetTypeActions> TypeActions = AssetTypeActions.Pin();
			FilterName = TypeActions->GetName();
		}
		else if ( FrontendFilter.IsValid() )
		{
			FilterName = FrontendFilter->GetDisplayName();
		}

		if ( FilterName.IsEmpty() )
		{
			FilterName = LOCTEXT("UnknownFilter", "???");
		}

		return FilterName;
	}

private:
	/** Invoked when the filter toggled */
	SFilterList::FOnFilterChanged OnFilterChanged;

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

	/** true when this filter should be applied to the search */
	bool bEnabled;

	/** The asset type actions that are associated with this filter */
	TWeakPtr<IAssetTypeActions> AssetTypeActions;

	/** If this is an front end filter, this is the filter object */
	TSharedPtr<FFrontendFilter> FrontendFilter;

	/** The button to toggle the filter on or off */
	TSharedPtr<SFilterCheckBox> ToggleButtonPtr;

	/** The color of the checkbox for this filter */
	FLinearColor FilterColor;
};


/////////////////////
// SFilterList
/////////////////////


void SFilterList::Construct( const FArguments& InArgs )
{
	OnGetContextMenu = InArgs._OnGetContextMenu;
	OnFilterChanged = InArgs._OnFilterChanged;
	FrontendFilters = InArgs._FrontendFilters;
	InitialClassFilters = InArgs._InitialClassFilters;

	TSharedPtr<FFrontendFilterCategory> DefaultCategory = MakeShareable( new FFrontendFilterCategory(LOCTEXT("FrontendFiltersCategory", "Other Filters"), LOCTEXT("FrontendFiltersCategoryTooltip", "Filter assets by all filters in this category.")) );

	// Add all built-in frontend filters here
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_CheckedOut(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_Modified(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_ShowOtherDevelopers(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_ReplicatedBlueprint(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_ShowRedirectors(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_InUseByLoadedLevels(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_UsedInAnyLevel(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_NotUsedInAnyLevel(DefaultCategory)) );
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_ArbitraryComparisonOperation(DefaultCategory)) );
	AllFrontendFilters.Add(MakeShareable(new FFrontendFilter_Recent(DefaultCategory)));
	AllFrontendFilters.Add( MakeShareable(new FFrontendFilter_NotSourceControlled(DefaultCategory)) );

	// Add any global user-defined frontend filters
	for (TObjectIterator<UContentBrowserFrontEndFilterExtension> ExtensionIt(RF_NoFlags); ExtensionIt; ++ExtensionIt)
	{
		if (UContentBrowserFrontEndFilterExtension* PotentialExtension = *ExtensionIt)
		{
			if (PotentialExtension->HasAnyFlags(RF_ClassDefaultObject) && !PotentialExtension->GetClass()->HasAnyClassFlags(CLASS_Deprecated | CLASS_Abstract))
			{
				// Grab the filters
				TArray< TSharedRef<FFrontendFilter> > ExtendedFrontendFilters;
				PotentialExtension->AddFrontEndFilterExtensions(DefaultCategory, ExtendedFrontendFilters);
				AllFrontendFilters.Append(ExtendedFrontendFilters);

				// Grab the categories
				for (const TSharedRef<FFrontendFilter>& FilterRef : ExtendedFrontendFilters)
				{
					TSharedPtr<FFrontendFilterCategory> Category = FilterRef->GetCategory();
					if (Category.IsValid())
					{
						AllFrontendFilterCategories.AddUnique(Category);
					}
				}
			}
		}
	}

	// Add in filters specific to this invocation
	for (const TSharedRef<FFrontendFilter>& Filter : InArgs._ExtraFrontendFilters)
	{
		if (TSharedPtr<FFrontendFilterCategory> Category = Filter->GetCategory())
		{
			AllFrontendFilterCategories.AddUnique(Category);
		}

		AllFrontendFilters.Add(Filter);
	}

	AllFrontendFilterCategories.AddUnique(DefaultCategory);

	
	for (const TSharedRef<FFrontendFilter>& Filter : AllFrontendFilters)
	{
		// Bind external activation event
		FFrontendFilterExternalActivationHelper::BindToFilter(SharedThis(this), Filter);

		// Auto add all inverse filters
		SetFrontendFilterActive(Filter, false);
	}

	FilterBox = SNew(SWrapBox)
		.UseAllottedWidth(true);

	ChildSlot
	[
		FilterBox.ToSharedRef()
	];
}

FReply SFilterList::OnMouseButtonUp( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if ( MouseEvent.GetEffectingButton() == EKeys::RightMouseButton )
	{
		if ( OnGetContextMenu.IsBound() )
		{
			FReply Reply = FReply::Handled().ReleaseMouseCapture();

			// Get the context menu content. If NULL, don't open a menu.
			TSharedPtr<SWidget> MenuContent = OnGetContextMenu.Execute();

			if ( MenuContent.IsValid() )
			{
				FVector2D SummonLocation = MouseEvent.GetScreenSpacePosition();
				FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
				FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuContent.ToSharedRef(), SummonLocation, FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
			}

			return Reply;
		}
	}

	return FReply::Unhandled();
}

const TArray<UClass*>& SFilterList::GetInitialClassFilters()
{
	return InitialClassFilters;
}

bool SFilterList::HasAnyFilters() const
{
	return Filters.Num() > 0;
}

FARFilter SFilterList::GetCombinedBackendFilter() const
{
	FARFilter CombinedFilter;

	// Add all selected filters
	for (int32 FilterIdx = 0; FilterIdx < Filters.Num(); ++FilterIdx)
	{
		if ( Filters[FilterIdx]->IsEnabled() )
		{
			CombinedFilter.Append(Filters[FilterIdx]->GetBackendFilter());
		}
	}

	if ( CombinedFilter.bRecursiveClasses )
	{
		// Add exclusions for AssetTypeActions NOT in the filter.
		// This will prevent assets from showing up that are both derived from an asset in the filter set and derived from an asset not in the filter set
		// Get the list of all asset type actions
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		TArray< TWeakPtr<IAssetTypeActions> > AssetTypeActionsList;
		AssetToolsModule.Get().GetAssetTypeActionsList(AssetTypeActionsList);
		for (const TWeakPtr<IAssetTypeActions>& WeakTypeActions : AssetTypeActionsList)
		{
			if (const TSharedPtr<IAssetTypeActions> TypeActions = WeakTypeActions.Pin())
			{
				if (TypeActions->CanFilter())
				{
					const UClass* TypeClass = TypeActions->GetSupportedClass();
					if (!CombinedFilter.ClassNames.Contains(TypeClass->GetFName()))
					{
						CombinedFilter.RecursiveClassesExclusionSet.Add(TypeClass->GetFName());
					}
				}
			}
		}
	}

	// HACK: A blueprint can be shown as Blueprint or as BlueprintGeneratedClass, but we don't want to distinguish them while filtering.
	// This should be removed, once all blueprints are shown as BlueprintGeneratedClass.
	if(CombinedFilter.ClassNames.Contains(FName(TEXT("Blueprint"))))
	{
		CombinedFilter.ClassNames.AddUnique(FName(TEXT("BlueprintGeneratedClass")));
	}

	return CombinedFilter;
}

TSharedPtr<FFrontendFilter> SFilterList::GetFrontendFilter(const FString& InName) const
{
	for (const TSharedRef<FFrontendFilter>& Filter : AllFrontendFilters)
	{
		if (Filter->GetName() == InName)
		{
			return Filter;
		}
	}
	return TSharedPtr<FFrontendFilter>();
}

TSharedRef<SWidget> SFilterList::ExternalMakeAddFilterMenu(EAssetTypeCategories::Type MenuExpansion)
{
	return MakeAddFilterMenu(MenuExpansion);
}

void SFilterList::EnableAllFilters()
{
	for (const TSharedRef<SFilter>& Filter : Filters)
	{
		Filter->SetEnabled(true, false);
	}

	OnFilterChanged.ExecuteIfBound();
}

void SFilterList::DisableAllFilters()
{
	for (const TSharedRef<SFilter>& Filter : Filters)
	{
		Filter->SetEnabled(false, false);
	}

	OnFilterChanged.ExecuteIfBound();
}

void SFilterList::RemoveAllFilters()
{
	if (HasAnyFilters())
	{
		// Update the frontend filters collection
		for (const TSharedRef<SFilter>& FilterToRemove : Filters)
		{
			if (const TSharedPtr<FFrontendFilter>& FrontendFilter = FilterToRemove->GetFrontendFilter())
			{
				SetFrontendFilterActive(FrontendFilter.ToSharedRef(), false); // Deactivate.
			}
		}

		FilterBox->ClearChildren();
		Filters.Empty();

		// Notify that a filter has changed
		OnFilterChanged.ExecuteIfBound();
	}
}

void SFilterList::DisableFiltersThatHideAssets(const TArray<FAssetData>& AssetDataList)
{
	if (HasAnyFilters())
	{
		// Determine if we should disable backend filters. If any asset fails the combined backend filter, disable them all.
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		FARFilter CombinedBackendFilter = GetCombinedBackendFilter();
		bool bDisableAllBackendFilters = false;
		TArray<FAssetData> LocalAssetDataList = AssetDataList;
		AssetRegistryModule.Get().RunAssetsThroughFilter(LocalAssetDataList, CombinedBackendFilter);
		if (LocalAssetDataList.Num() != AssetDataList.Num())
		{
			bDisableAllBackendFilters = true;
		}

		// Iterate over all enabled filters and disable any frontend filters that would hide any of the supplied assets
		// and disable all backend filters if it was determined that the combined backend filter hides any of the assets
		bool ExecuteOnFilteChanged = false;
		for (const TSharedRef<SFilter>& Filter : Filters)
		{
			if (Filter->IsEnabled())
			{
				if (const TSharedPtr<FFrontendFilter>& FrontendFilter = Filter->GetFrontendFilter())
				{
					for (const FAssetData& AssetData : AssetDataList)
					{
						if (!FrontendFilter->IsInverseFilter() && !FrontendFilter->PassesFilter(AssetData))
						{
							// This is a frontend filter and at least one asset did not pass.
							Filter->SetEnabled(false, false);
							ExecuteOnFilteChanged = true;
						}
					}
				}

				if (bDisableAllBackendFilters)
				{
					FARFilter BackendFilter = Filter->GetBackendFilter();
					if (!BackendFilter.IsEmpty())
					{
						Filter->SetEnabled(false, false);
						ExecuteOnFilteChanged = true;
					}
				}
			}
		}

		if (ExecuteOnFilteChanged)
		{
			OnFilterChanged.ExecuteIfBound();
		}
	}
}

void SFilterList::SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const
{
	FString ActiveTypeFilterString;
	FString EnabledTypeFilterString;
	FString ActiveFrontendFilterString;
	FString EnabledFrontendFilterString;
	for ( auto FilterIt = Filters.CreateConstIterator(); FilterIt; ++FilterIt )
	{
		const TSharedRef<SFilter>& Filter = *FilterIt;

		if ( Filter->GetAssetTypeActions().IsValid() )
		{
			if ( ActiveTypeFilterString.Len() > 0 )
			{
				ActiveTypeFilterString += TEXT(",");
			}

			const FString FilterName = Filter->GetAssetTypeActions().Pin()->GetSupportedClass()->GetName();
			ActiveTypeFilterString += FilterName;

			if ( Filter->IsEnabled() )
			{
				if ( EnabledTypeFilterString.Len() > 0 )
				{
					EnabledTypeFilterString += TEXT(",");
				}

				EnabledTypeFilterString += FilterName;
			}
		}
		else if ( Filter->GetFrontendFilter().IsValid() )
		{
			const TSharedPtr<FFrontendFilter>& FrontendFilter = Filter->GetFrontendFilter();
			if ( ActiveFrontendFilterString.Len() > 0 )
			{
				ActiveFrontendFilterString += TEXT(",");
			}

			const FString FilterName = FrontendFilter->GetName();
			ActiveFrontendFilterString += FilterName;

			if ( Filter->IsEnabled() )
			{
				if ( EnabledFrontendFilterString.Len() > 0 )
				{
					EnabledFrontendFilterString += TEXT(",");
				}

				EnabledFrontendFilterString += FilterName;
			}

			const FString CustomSettingsString = FString::Printf(TEXT("%s.CustomSettings.%s"), *SettingsString, *FilterName);
			FrontendFilter->SaveSettings(IniFilename, IniSection, CustomSettingsString);
		}
	}

	GConfig->SetString(*IniSection, *(SettingsString + TEXT(".ActiveTypeFilters")), *ActiveTypeFilterString, IniFilename);
	GConfig->SetString(*IniSection, *(SettingsString + TEXT(".EnabledTypeFilters")), *EnabledTypeFilterString, IniFilename);
	GConfig->SetString(*IniSection, *(SettingsString + TEXT(".ActiveFrontendFilters")), *ActiveFrontendFilterString, IniFilename);
	GConfig->SetString(*IniSection, *(SettingsString + TEXT(".EnabledFrontendFilters")), *EnabledFrontendFilterString, IniFilename);
}

void SFilterList::LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString)
{
	{
		// Add all the type filters that were found in the ActiveTypeFilters
		FString ActiveTypeFilterString;
		FString EnabledTypeFilterString;
		GConfig->GetString(*IniSection, *(SettingsString + TEXT(".ActiveTypeFilters")), ActiveTypeFilterString, IniFilename);
		GConfig->GetString(*IniSection, *(SettingsString + TEXT(".EnabledTypeFilters")), EnabledTypeFilterString, IniFilename);

		// Parse comma delimited strings into arrays
		TArray<FString> TypeFilterNames;
		TArray<FString> EnabledTypeFilterNames;
		ActiveTypeFilterString.ParseIntoArray(TypeFilterNames, TEXT(","), /*bCullEmpty=*/true);
		EnabledTypeFilterString.ParseIntoArray(EnabledTypeFilterNames, TEXT(","), /*bCullEmpty=*/true);

		// Get the list of all asset type actions
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		TArray< TWeakPtr<IAssetTypeActions> > AssetTypeActionsList;
		AssetToolsModule.Get().GetAssetTypeActionsList(AssetTypeActionsList);

		// For each TypeActions, add any that were active and enable any that were previously enabled
		for ( auto TypeActionsIt = AssetTypeActionsList.CreateConstIterator(); TypeActionsIt; ++TypeActionsIt )
		{
			const TWeakPtr<IAssetTypeActions>& TypeActions = *TypeActionsIt;
			if ( TypeActions.IsValid() && TypeActions.Pin()->CanFilter() && !IsAssetTypeActionsInUse(TypeActions) )
			{
				const FString& ClassName = TypeActions.Pin()->GetSupportedClass()->GetName();
				if ( TypeFilterNames.Contains(ClassName) )
				{
					TSharedRef<SFilter> NewFilter = AddFilter(TypeActions);

					if ( EnabledTypeFilterNames.Contains(ClassName) )
					{
						NewFilter->SetEnabled(true, false);
					}
				}
			}
		}
	}

	{
		// Add all the frontend filters that were found in the ActiveFrontendFilters
		FString ActiveFrontendFilterString;	
		FString EnabledFrontendFilterString;
		GConfig->GetString(*IniSection, *(SettingsString + TEXT(".ActiveFrontendFilters")), ActiveFrontendFilterString, IniFilename);
		GConfig->GetString(*IniSection, *(SettingsString + TEXT(".EnabledFrontendFilters")), EnabledFrontendFilterString, IniFilename);

		// Parse comma delimited strings into arrays
		TArray<FString> FrontendFilterNames;
		TArray<FString> EnabledFrontendFilterNames;
		ActiveFrontendFilterString.ParseIntoArray(FrontendFilterNames, TEXT(","), /*bCullEmpty=*/true);
		EnabledFrontendFilterString.ParseIntoArray(EnabledFrontendFilterNames, TEXT(","), /*bCullEmpty=*/true);

		// For each FrontendFilter, add any that were active and enable any that were previously enabled
		for ( auto FrontendFilterIt = AllFrontendFilters.CreateIterator(); FrontendFilterIt; ++FrontendFilterIt )
		{
			TSharedRef<FFrontendFilter>& FrontendFilter = *FrontendFilterIt;
			const FString& FilterName = FrontendFilter->GetName();
			if (!IsFrontendFilterInUse(FrontendFilter))
			{
				if ( FrontendFilterNames.Contains(FilterName) )
				{
					TSharedRef<SFilter> NewFilter = AddFilter(FrontendFilter);

					if ( EnabledFrontendFilterNames.Contains(FilterName) )
					{
						NewFilter->SetEnabled(true, false);
						SetFrontendFilterActive(FrontendFilter, NewFilter->IsEnabled());
					}
				}
			}

			const FString CustomSettingsString = FString::Printf(TEXT("%s.CustomSettings.%s"), *SettingsString, *FilterName);
			FrontendFilter->LoadSettings(IniFilename, IniSection, CustomSettingsString);
		}
	}

	OnFilterChanged.ExecuteIfBound();
}

void SFilterList::SetFrontendFilterCheckState(const TSharedPtr<FFrontendFilter>& InFrontendFilter, ECheckBoxState InCheckState)
{
	if (!InFrontendFilter || InCheckState == ECheckBoxState::Undetermined)
	{
		return;
	}

	// Check if the filter is already checked.
	TSharedRef<FFrontendFilter> FrontendFilter = InFrontendFilter.ToSharedRef();
	bool FrontendFilterChecked = IsFrontendFilterInUse(FrontendFilter);

	if (InCheckState == ECheckBoxState::Checked && !FrontendFilterChecked)
	{
		AddFilter(FrontendFilter)->SetEnabled(true); // Pin a filter widget on the UI and activate the filter. Same behaviour as FrontendFilterClicked()
	}
	else if (InCheckState == ECheckBoxState::Unchecked && FrontendFilterChecked)
	{
		RemoveFilter(FrontendFilter); // Unpin the filter widget and deactivate the filter.
	}
	// else -> Already in the desired 'check' state.
}

ECheckBoxState SFilterList::GetFrontendFilterCheckState(const TSharedPtr<FFrontendFilter>& InFrontendFilter) const
{
	return InFrontendFilter && IsFrontendFilterInUse(InFrontendFilter.ToSharedRef()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

bool SFilterList::IsFrontendFilterActive(const TSharedPtr<FFrontendFilter>& InFrontendFilter) const
{
	if (InFrontendFilter.IsValid())
	{
		for (const TSharedRef<SFilter>& Filter : Filters)
		{
			if (InFrontendFilter == Filter->GetFrontendFilter())
			{
				return Filter->IsEnabled(); // Is active or not?
			}
		}
	}
	return false;
}

void SFilterList::SetFrontendFilterActive(const TSharedRef<FFrontendFilter>& Filter, bool bActive)
{
	if(Filter->IsInverseFilter())
	{
		//Inverse filters are active when they are "disabled"
		bActive = !bActive;
	}
	Filter->ActiveStateChanged(bActive);

	if ( bActive )
	{
		FrontendFilters->Add(Filter);
	}
	else
	{
		FrontendFilters->Remove(Filter);
	}
}

TSharedRef<SFilter> SFilterList::AddFilter(const TWeakPtr<IAssetTypeActions>& AssetTypeActions)
{
	TSharedRef<SFilter> NewFilter =
		SNew(SFilter)
		.AssetTypeActions(AssetTypeActions)
		.OnFilterChanged(OnFilterChanged)
		.OnRequestRemove(this, &SFilterList::RemoveFilterAndUpdate)
		.OnRequestEnableOnly(this, &SFilterList::EnableOnlyThisFilter)
		.OnRequestEnableAll(this, &SFilterList::EnableAllFilters)
		.OnRequestDisableAll(this, &SFilterList::DisableAllFilters)
		.OnRequestRemoveAll(this, &SFilterList::RemoveAllFilters);

	AddFilter( NewFilter );

	return NewFilter;
}

TSharedRef<SFilter> SFilterList::AddFilter(const TSharedRef<FFrontendFilter>& FrontendFilter)
{
	TSharedRef<SFilter> NewFilter =
		SNew(SFilter)
		.FrontendFilter(FrontendFilter)
		.OnFilterChanged( this, &SFilterList::FrontendFilterChanged, FrontendFilter )
		.OnRequestRemove(this, &SFilterList::RemoveFilterAndUpdate)
		.OnRequestEnableAll(this, &SFilterList::EnableAllFilters)
		.OnRequestDisableAll(this, &SFilterList::DisableAllFilters)
		.OnRequestRemoveAll(this, &SFilterList::RemoveAllFilters);

	AddFilter( NewFilter );

	return NewFilter;
}

void SFilterList::AddFilter(const TSharedRef<SFilter>& FilterToAdd)
{
	Filters.Add(FilterToAdd);

	FilterBox->AddSlot()
	.Padding(3, 3)
	[
		FilterToAdd
	];
}

void SFilterList::RemoveFilter(const TWeakPtr<IAssetTypeActions>& AssetTypeActions, bool ExecuteOnFilterChanged)
{
	TSharedPtr<SFilter> FilterToRemove;
	for ( auto FilterIt = Filters.CreateConstIterator(); FilterIt; ++FilterIt )
	{
		const TWeakPtr<IAssetTypeActions>& Actions = (*FilterIt)->GetAssetTypeActions();
		if ( Actions.IsValid() && Actions == AssetTypeActions)
		{
			FilterToRemove = *FilterIt;
			break;
		}
	}

	if ( FilterToRemove.IsValid() )
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

void SFilterList::EnableOnlyThisFilter(const TSharedRef<SFilter>& FilterToEnable)
{
	for (const TSharedRef<SFilter>& Filter : Filters)
	{
		bool bEnable = Filter == FilterToEnable;
		Filter->SetEnabled(bEnable, /*ExecuteOnFilterChange*/false);
	}

	OnFilterChanged.ExecuteIfBound();
}

void SFilterList::RemoveFilter(const TSharedRef<FFrontendFilter>& FrontendFilter, bool ExecuteOnFilterChanged)
{
	TSharedPtr<SFilter> FilterToRemove;
	for (const TSharedRef<SFilter>& Filter : Filters)
	{
		if (Filter->GetFrontendFilter() == FrontendFilter)
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

void SFilterList::RemoveFilter(const TSharedRef<SFilter>& FilterToRemove)
{
	FilterBox->RemoveSlot(FilterToRemove);
	Filters.Remove(FilterToRemove);

	if (const TSharedPtr<FFrontendFilter>& FrontendFilter = FilterToRemove->GetFrontendFilter()) // Is valid?
	{
		// Update the frontend filters collection
		SetFrontendFilterActive(FrontendFilter.ToSharedRef(), false);
		OnFilterChanged.ExecuteIfBound();
	}
}

void SFilterList::RemoveFilterAndUpdate(const TSharedRef<SFilter>& FilterToRemove)
{
	RemoveFilter(FilterToRemove);

	// Notify that a filter has changed
	OnFilterChanged.ExecuteIfBound();
}

void SFilterList::FrontendFilterChanged(TSharedRef<FFrontendFilter> FrontendFilter)
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

void SFilterList::CreateFiltersMenuCategory(FMenuBuilder& MenuBuilder, const TArray<TWeakPtr<IAssetTypeActions>> AssetTypeActionsList) const
{
	for (int32 ClassIdx = 0; ClassIdx < AssetTypeActionsList.Num(); ++ClassIdx)
	{
		const TWeakPtr<IAssetTypeActions>& WeakTypeActions = AssetTypeActionsList[ClassIdx];
		if ( WeakTypeActions.IsValid() )
		{
			TSharedPtr<IAssetTypeActions> TypeActions = WeakTypeActions.Pin();
			if ( TypeActions.IsValid() )
			{
				const FText& LabelText = TypeActions->GetName();
				MenuBuilder.AddMenuEntry(
					LabelText,
					FText::Format( LOCTEXT("FilterByTooltipPrefix", "Filter by {0}"), LabelText ),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP( const_cast<SFilterList*>(this), &SFilterList::FilterByTypeClicked, WeakTypeActions ),
						FCanExecuteAction(),
						FIsActionChecked::CreateSP(this, &SFilterList::IsAssetTypeActionsInUse, WeakTypeActions ) ),
					NAME_None,
					EUserInterfaceActionType::ToggleButton
					);
			}
		}
	}
}

void SFilterList::CreateOtherFiltersMenuCategory(FMenuBuilder& MenuBuilder, TSharedPtr<FFrontendFilterCategory> MenuCategory) const
{
	for (const TSharedRef<FFrontendFilter>& FrontendFilter : AllFrontendFilters)
	{
		if(FrontendFilter->GetCategory() == MenuCategory)
		{
			MenuBuilder.AddMenuEntry(
				FrontendFilter->GetDisplayName(),
				FrontendFilter->GetToolTipText(),
				FSlateIcon(FEditorStyle::GetStyleSetName(), FrontendFilter->GetIconName()),
				FUIAction(
				FExecuteAction::CreateSP(const_cast<SFilterList*>(this), &SFilterList::FrontendFilterClicked, FrontendFilter),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SFilterList::IsFrontendFilterInUse, FrontendFilter)),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
				);
		}
	}
}

bool IsFilteredByPicker(const TArray<UClass*>& FilterClassList, UClass* TestClass)
{
	if (FilterClassList.Num() == 0)
	{
		return false;
	}
	for (const UClass* Class : FilterClassList)
	{
		if (TestClass->IsChildOf(Class))
		{
			return false;
		}
	}
	return true;
}

TSharedRef<SWidget> SFilterList::MakeAddFilterMenu(EAssetTypeCategories::Type MenuExpansion)
{
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));

	// A local struct to describe a category in the filter menu
	struct FCategoryMenu
	{
		FText Name;
		FText Tooltip;
		TArray<TWeakPtr<IAssetTypeActions>> Assets;

		//Menu section
		FName SectionExtensionHook;
		FText SectionHeading;

		FCategoryMenu(const FText& InName, const FText& InTooltip, const FName& InSectionExtensionHook, const FText& InSectionHeading)
			: Name(InName)
			, Tooltip(InTooltip)
			, Assets()
			, SectionExtensionHook(InSectionExtensionHook)
			, SectionHeading(InSectionHeading)
		{}
	};

	// Create a map of Categories to Menus
	TMap<EAssetTypeCategories::Type, FCategoryMenu> CategoryToMenuMap;

	// Add the Basic category
	CategoryToMenuMap.Add(EAssetTypeCategories::Basic, FCategoryMenu( LOCTEXT("BasicFilter", "Basic"), LOCTEXT("BasicFilterTooltip", "Filter by basic assets."), "ContentBrowserFilterBasicAsset", LOCTEXT("BasicAssetsMenuHeading", "Basic Assets") ) );

	// Add the advanced categories
	TArray<FAdvancedAssetCategory> AdvancedAssetCategories;
	AssetToolsModule.Get().GetAllAdvancedAssetCategories(/*out*/ AdvancedAssetCategories);

	for (const FAdvancedAssetCategory& AdvancedAssetCategory : AdvancedAssetCategories)
	{
		const FName ExtensionPoint = NAME_None;
		const FText SectionHeading = FText::Format(LOCTEXT("WildcardFilterHeadingHeadingTooltip", "{0} Assets."), AdvancedAssetCategory.CategoryName);
		const FText Tooltip = FText::Format(LOCTEXT("WildcardFilterTooltip", "Filter by {0}."), SectionHeading);
		CategoryToMenuMap.Add(AdvancedAssetCategory.CategoryType, FCategoryMenu(AdvancedAssetCategory.CategoryName, Tooltip, ExtensionPoint, SectionHeading));
	}

	// Get the browser type maps
	TArray<TWeakPtr<IAssetTypeActions>> AssetTypeActionsList;
	AssetToolsModule.Get().GetAssetTypeActionsList(AssetTypeActionsList);

	// Sort the list
	struct FCompareIAssetTypeActions
	{
		FORCEINLINE bool operator()( const TWeakPtr<IAssetTypeActions>& A, const TWeakPtr<IAssetTypeActions>& B ) const
		{
			return A.Pin()->GetName().CompareTo( B.Pin()->GetName() ) == -1;
		}
	};
	AssetTypeActionsList.Sort( FCompareIAssetTypeActions() );

	TSharedRef<FBlacklistNames> AssetClassBlacklist = AssetToolsModule.Get().GetAssetClassBlacklist();

	// For every asset type, move it into all the categories it should appear in
	for (int32 ClassIdx = 0; ClassIdx < AssetTypeActionsList.Num(); ++ClassIdx)
	{
		const TWeakPtr<IAssetTypeActions>& WeakTypeActions = AssetTypeActionsList[ClassIdx];
		if ( WeakTypeActions.IsValid() )
		{
			TSharedPtr<IAssetTypeActions> TypeActions = WeakTypeActions.Pin();
			if ( ensure(TypeActions.IsValid()) && TypeActions->CanFilter() )
			{
				UClass* SupportedClass = TypeActions->GetSupportedClass();
				if ((!SupportedClass || AssetClassBlacklist->PassesFilter(SupportedClass->GetFName())) && !IsFilteredByPicker(InitialClassFilters, SupportedClass))
				{
					for ( auto MenuIt = CategoryToMenuMap.CreateIterator(); MenuIt; ++MenuIt )
					{
						if ( TypeActions->GetCategories() & MenuIt.Key() )
						{
							// This is a valid asset type which can be filtered, add it to the correct category
							FCategoryMenu& Menu = MenuIt.Value();
							Menu.Assets.Add( WeakTypeActions );
						}
					}
				}
			}
		}
	}

	for (auto MenuIt = CategoryToMenuMap.CreateIterator(); MenuIt; ++MenuIt)
	{
		if (MenuIt.Value().Assets.Num() == 0)
		{
			CategoryToMenuMap.Remove(MenuIt.Key());
		}
	}

	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr, nullptr, /*bCloseSelfOnly=*/true);

	MenuBuilder.BeginSection("ContentBrowserResetFilters");
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("FilterListResetFilters", "Reset Filters"),
			LOCTEXT("FilterListResetToolTip", "Resets current filter selection"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SFilterList::OnResetFilters))
			);
	}
	MenuBuilder.EndSection(); //ContentBrowserResetFilters

	// First add the expanded category, this appears as standard entries in the list (Note: intentionally not using FindChecked here as removing it from the map later would cause the ref to be garbage)
	FCategoryMenu* ExpandedCategory = CategoryToMenuMap.Find( MenuExpansion );
	check( ExpandedCategory );

	MenuBuilder.BeginSection(ExpandedCategory->SectionExtensionHook, ExpandedCategory->SectionHeading );
	{
		if(MenuExpansion == EAssetTypeCategories::Basic)
		{
			// If we are doing a full menu (i.e expanding basic) we add a menu entry which toggles all other categories
			MenuBuilder.AddMenuEntry(
				ExpandedCategory->Name,
				ExpandedCategory->Tooltip,
				FSlateIcon(),
				FUIAction(
				FExecuteAction::CreateSP( this, &SFilterList::FilterByTypeCategoryClicked, MenuExpansion ),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SFilterList::IsAssetTypeCategoryInUse, MenuExpansion ) ),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
				);
		}

		// Now populate with all the basic assets
		SFilterList::CreateFiltersMenuCategory( MenuBuilder, ExpandedCategory->Assets);
	}
	MenuBuilder.EndSection(); //ContentBrowserFilterBasicAsset

	// Remove the basic category from the map now, as this is treated differently and is no longer needed.
	ExpandedCategory = nullptr;
	CategoryToMenuMap.Remove(EAssetTypeCategories::Basic);

	// If we have expanded Basic, assume we are in full menu mode and add all the other categories
	MenuBuilder.BeginSection("ContentBrowserFilterAdvancedAsset", LOCTEXT("AdvancedAssetsMenuHeading", "Other Assets"));
	{
		if(MenuExpansion == EAssetTypeCategories::Basic)
		{
			// For all the remaining categories, add them as submenus
			for (const TPair<EAssetTypeCategories::Type, FCategoryMenu>& CategoryMenuPair : CategoryToMenuMap)
			{
				MenuBuilder.AddSubMenu(
					CategoryMenuPair.Value.Name,
					CategoryMenuPair.Value.Tooltip,
					FNewMenuDelegate::CreateSP(this, &SFilterList::CreateFiltersMenuCategory, CategoryMenuPair.Value.Assets),
					FUIAction(
					FExecuteAction::CreateSP(this, &SFilterList::FilterByTypeCategoryClicked, CategoryMenuPair.Key),
					FCanExecuteAction(),
					FIsActionChecked::CreateSP(this, &SFilterList::IsAssetTypeCategoryInUse, CategoryMenuPair.Key)),
					NAME_None,
					EUserInterfaceActionType::ToggleButton
					);
			}
		}

		// Now add the other filter which aren't assets
		for (const TSharedPtr<FFrontendFilterCategory>& Category : AllFrontendFilterCategories)
		{
			MenuBuilder.AddSubMenu(
				Category->Title,
				Category->Tooltip,
				FNewMenuDelegate::CreateSP(this, &SFilterList::CreateOtherFiltersMenuCategory, Category),
				FUIAction(
				FExecuteAction::CreateSP( this, &SFilterList::FrontendFilterCategoryClicked, Category ),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP(this, &SFilterList::IsFrontendFilterCategoryInUse, Category ) ),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
				);
		}
	}
	MenuBuilder.EndSection(); //ContentBrowserFilterAdvancedAsset

	MenuBuilder.BeginSection("ContentBrowserFilterMiscAsset", LOCTEXT("MiscAssetsMenuHeading", "Misc Options") );
	MenuBuilder.EndSection(); //ContentBrowserFilterMiscAsset

	FDisplayMetrics DisplayMetrics;
	FSlateApplication::Get().GetCachedDisplayMetrics( DisplayMetrics );

	const FVector2D DisplaySize(
		DisplayMetrics.PrimaryDisplayWorkAreaRect.Right - DisplayMetrics.PrimaryDisplayWorkAreaRect.Left,
		DisplayMetrics.PrimaryDisplayWorkAreaRect.Bottom - DisplayMetrics.PrimaryDisplayWorkAreaRect.Top );

	return 
		SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.MaxHeight(DisplaySize.Y * 0.9)
		[
			MenuBuilder.MakeWidget()
		];
}

void SFilterList::FilterByTypeClicked(TWeakPtr<IAssetTypeActions> AssetTypeActions)
{
	if (AssetTypeActions.IsValid())
	{
		if (IsAssetTypeActionsInUse(AssetTypeActions))
		{
			RemoveFilter(AssetTypeActions);
		}
		else
		{
			TSharedRef<SFilter> NewFilter = AddFilter(AssetTypeActions);
			NewFilter->SetEnabled(true);
		}
	}
}

bool SFilterList::IsAssetTypeActionsInUse(TWeakPtr<IAssetTypeActions> AssetTypeActions) const
{
	if (!AssetTypeActions.IsValid())
	{
		return false;
	}

	TSharedPtr<IAssetTypeActions> TypeActions = AssetTypeActions.Pin();
	if (!TypeActions.IsValid())
	{
		return false;
	}

	for (const TSharedRef<SFilter>& Filter : Filters)
	{
		if (Filter->GetAssetTypeActions().Pin() == TypeActions)
		{
			return true;
		}
	}

	return false;
}

void SFilterList::FilterByTypeCategoryClicked(EAssetTypeCategories::Type Category)
{
	TArray<TWeakPtr<IAssetTypeActions>> TypeActionsList;
	GetTypeActionsForCategory(Category, TypeActionsList);

	bool bFullCategoryInUse = IsAssetTypeCategoryInUse(Category);
	bool ExecuteOnFilterChanged = false;

	for (const TWeakPtr<IAssetTypeActions>& AssetTypeActions : TypeActionsList)
	{
		if (AssetTypeActions.IsValid())
		{
			if (bFullCategoryInUse)
			{
				RemoveFilter(AssetTypeActions);
				ExecuteOnFilterChanged = true;
			}
			else if (!IsAssetTypeActionsInUse(AssetTypeActions))
			{
				TSharedRef<SFilter> NewFilter = AddFilter(AssetTypeActions);
				NewFilter->SetEnabled(true, false);
				ExecuteOnFilterChanged = true;
			}
		}
	}

	if (ExecuteOnFilterChanged)
	{
		OnFilterChanged.ExecuteIfBound();
	}
}

bool SFilterList::IsAssetTypeCategoryInUse(EAssetTypeCategories::Type Category) const
{
	TArray<TWeakPtr<IAssetTypeActions>> TypeActionsList;
	GetTypeActionsForCategory(Category, TypeActionsList);

	for (const TWeakPtr<IAssetTypeActions>& AssetTypeActions : TypeActionsList)
	{
		if (AssetTypeActions.IsValid())
		{
			if (!IsAssetTypeActionsInUse(AssetTypeActions))
			{
				return false;
			}
		}
	}

	return true;
}

void SFilterList::GetTypeActionsForCategory(EAssetTypeCategories::Type Category, TArray< TWeakPtr<IAssetTypeActions> >& TypeActions) const
{
	// Load the asset tools module
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	TArray<TWeakPtr<IAssetTypeActions>> AssetTypeActionsList;
	AssetToolsModule.Get().GetAssetTypeActionsList(AssetTypeActionsList);
	TSharedRef<FBlacklistNames> AssetClassBlacklist = AssetToolsModule.Get().GetAssetClassBlacklist();

	// Find all asset type actions that match the category
	for (int32 ClassIdx = 0; ClassIdx < AssetTypeActionsList.Num(); ++ClassIdx)
	{
		const TWeakPtr<IAssetTypeActions>& WeakTypeActions = AssetTypeActionsList[ClassIdx];
		TSharedPtr<IAssetTypeActions> AssetTypeActions = WeakTypeActions.Pin();

		if (ensure(AssetTypeActions.IsValid()) && AssetTypeActions->CanFilter() && AssetTypeActions->GetCategories() & Category)
		{
			if (AssetTypeActions->GetSupportedClass() == nullptr || AssetClassBlacklist->PassesFilter(AssetTypeActions->GetSupportedClass()->GetFName()))
			{
				TypeActions.Add(WeakTypeActions);
			}
		}
	}
}

void SFilterList::FrontendFilterClicked(TSharedRef<FFrontendFilter> FrontendFilter)
{
	if (IsFrontendFilterInUse(FrontendFilter))
	{
		RemoveFilter(FrontendFilter);
	}
	else
	{
		TSharedRef<SFilter> NewFilter = AddFilter(FrontendFilter);
		NewFilter->SetEnabled(true);
	}
}

bool SFilterList::IsFrontendFilterInUse(TSharedRef<FFrontendFilter> FrontendFilter) const
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

void SFilterList::FrontendFilterCategoryClicked(TSharedPtr<FFrontendFilterCategory> MenuCategory)
{
	bool bFullCategoryInUse = IsFrontendFilterCategoryInUse(MenuCategory);
	bool ExecuteOnFilterChanged = false;

	for (const TSharedRef<FFrontendFilter>& FrontendFilter : AllFrontendFilters)
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
				TSharedRef<SFilter> NewFilter = AddFilter(FrontendFilter);
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

bool SFilterList::IsFrontendFilterCategoryInUse(TSharedPtr<FFrontendFilterCategory> MenuCategory) const
{
	for (const TSharedRef<FFrontendFilter>& FrontendFilter : AllFrontendFilters)
	{
		if (FrontendFilter->GetCategory() == MenuCategory && !IsFrontendFilterInUse(FrontendFilter))
		{
			return false;
		}
	}

	return true;
}

void SFilterList::OnResetFilters()
{
	RemoveAllFilters();
}

void SFilterList::OnSetFilterActive(bool bInActive, TWeakPtr<FFrontendFilter> InWeakFilter)
{
	TSharedPtr<FFrontendFilter> Filter = InWeakFilter.Pin();
	if (Filter.IsValid())
	{
		if (!IsFrontendFilterInUse(Filter.ToSharedRef()))
		{
			TSharedRef<SFilter> NewFilter = AddFilter(Filter.ToSharedRef());
			NewFilter->SetEnabled(bInActive);
		}
	}
}

#undef LOCTEXT_NAMESPACE
