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
#include "IContentBrowserDataModule.h"
#include "ContentBrowserDataSource.h"
#include "ContentBrowserDataSubsystem.h"
#include "ContentBrowserUtils.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "FrontendFilters.h"
#include "ContentBrowserFrontEndFilterExtension.h"
#include "Misc/NamePermissionList.h"
#include "ToolMenus.h"
#include "ContentBrowserMenuContexts.h"
#include "Widgets/Images/SImage.h"
#include "Filters/GenericFilter.h"

#define LOCTEXT_NAMESPACE "ContentBrowser"

/////////////////////
// SFilterList
/////////////////////


void SFilterList::Construct( const FArguments& InArgs )
{
	OnGetContextMenu = InArgs._OnGetContextMenu;
	this->OnFilterChanged = InArgs._OnFilterChanged;
	this->ActiveFilters = InArgs._FrontendFilters;
	InitialClassFilters = InArgs._InitialClassFilters;

	TSharedPtr<FFrontendFilterCategory> DefaultCategory = MakeShareable( new FFrontendFilterCategory(LOCTEXT("FrontendFiltersCategory", "Other Filters"), LOCTEXT("FrontendFiltersCategoryTooltip", "Filter assets by all filters in this category.")) );
	
	// Add all built-in frontend filters here
	AllFrontendFilters_Internal.Add( MakeShareable(new FFrontendFilter_CheckedOut(DefaultCategory)) );
	AllFrontendFilters_Internal.Add( MakeShareable(new FFrontendFilter_Modified(DefaultCategory)) );
	AllFrontendFilters_Internal.Add( MakeShareable(new FFrontendFilter_Writable(DefaultCategory)) );
	AllFrontendFilters_Internal.Add( MakeShareable(new FFrontendFilter_ShowOtherDevelopers(DefaultCategory)) );
	AllFrontendFilters_Internal.Add( MakeShareable(new FFrontendFilter_ReplicatedBlueprint(DefaultCategory)) );
	AllFrontendFilters_Internal.Add( MakeShareable(new FFrontendFilter_ShowRedirectors(DefaultCategory)) );
	AllFrontendFilters_Internal.Add( MakeShareable(new FFrontendFilter_InUseByLoadedLevels(DefaultCategory)) );
	AllFrontendFilters_Internal.Add( MakeShareable(new FFrontendFilter_UsedInAnyLevel(DefaultCategory)) );
	AllFrontendFilters_Internal.Add( MakeShareable(new FFrontendFilter_NotUsedInAnyLevel(DefaultCategory)) );
	AllFrontendFilters_Internal.Add( MakeShareable(new FFrontendFilter_ArbitraryComparisonOperation(DefaultCategory)) );
	AllFrontendFilters_Internal.Add(MakeShareable(new FFrontendFilter_Recent(DefaultCategory)));
	AllFrontendFilters_Internal.Add( MakeShareable(new FFrontendFilter_NotSourceControlled(DefaultCategory)) );

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
				AllFrontendFilters_Internal.Append(ExtendedFrontendFilters);

				// Grab the categories
				for (const TSharedRef<FFrontendFilter>& FilterRef : ExtendedFrontendFilters)
				{
					TSharedPtr<FFilterCategory> Category = FilterRef->GetCategory();
					if (Category.IsValid())
					{
						this->AllFilterCategories.AddUnique(Category);
					}
				}
			}
		}
	}

	// Add in filters specific to this invocation
	for (const TSharedRef<FFrontendFilter>& Filter : InArgs._ExtraFrontendFilters)
	{
		if (TSharedPtr<FFilterCategory> Category = Filter->GetCategory())
		{
			this->AllFilterCategories.AddUnique(Category);
		}

		AllFrontendFilters_Internal.Add(Filter);
	}

	this->AllFilterCategories.AddUnique(DefaultCategory);

	// Add the local copy of all filters to SFilterBar's copy of all filters
	for(TSharedRef<FFrontendFilter> FrontendFilter : AllFrontendFilters_Internal)
	{
		this->AllFrontendFilters.Add(FrontendFilter);
	}
	
	SAssetFilterBar<FAssetFilterType>::FArguments Args;

	/** Explicitly setting this to true as it should ALWAYS be true for SFilterList */
	Args._UseDefaultAssetFilters = true;
	Args._OnFilterChanged = this->OnFilterChanged;
	
	SAssetFilterBar<FAssetFilterType>::Construct(Args);

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

TSharedPtr<FFrontendFilter> SFilterList::GetFrontendFilter(const FString& InName) const
{
	for (const TSharedRef<FFrontendFilter>& Filter : AllFrontendFilters_Internal)
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

void SFilterList::DisableFiltersThatHideItems(TArrayView<const FContentBrowserItem> ItemList)
{
	if (HasAnyFilters() && ItemList.Num() > 0)
	{
		// Determine if we should disable backend filters. If any item fails the combined backend filter, disable them all.
		bool bDisableAllBackendFilters = false;
		{
			FContentBrowserDataCompiledFilter CompiledDataFilter;
			{
				static const FName RootPath = "/";

				UContentBrowserDataSubsystem* ContentBrowserData = IContentBrowserDataModule::Get().GetSubsystem();

				FContentBrowserDataFilter DataFilter;
				DataFilter.bRecursivePaths = true;
				ContentBrowserUtils::AppendAssetFilterToContentBrowserFilter(GetCombinedBackendFilter(), nullptr, nullptr, DataFilter);

				ContentBrowserData->CompileFilter(RootPath, DataFilter, CompiledDataFilter);
			}

			for (const FContentBrowserItem& Item : ItemList)
			{
				if (!Item.IsFile())
				{
					continue;
				}

				FContentBrowserItem::FItemDataArrayView InternalItems = Item.GetInternalItems();
				for (const FContentBrowserItemData& InternalItem : InternalItems)
				{
					UContentBrowserDataSource* ItemDataSource = InternalItem.GetOwnerDataSource();
					if (!ItemDataSource->DoesItemPassFilter(InternalItem, CompiledDataFilter))
					{
						bDisableAllBackendFilters = true;
						break;
					}
				}

				if (bDisableAllBackendFilters)
				{
					break;
				}
			}
		}

		// Iterate over all enabled filters and disable any frontend filters that would hide any of the supplied assets
		bool ExecuteOnFilterChanged = false;
		for (const TSharedPtr<SFilter> Filter : Filters)
		{
			if (Filter->IsEnabled())
			{
				if (const TSharedPtr<FFilterBase<FAssetFilterType>>& FrontendFilter = Filter->GetFrontendFilter())
				{
					for (const FContentBrowserItem& Item : ItemList)
					{
						if (!FrontendFilter->IsInverseFilter() && !FrontendFilter->PassesFilter(Item))
						{
							// This is a frontend filter and at least one asset did not pass.
							Filter->SetEnabled(false, false);
							SetFrontendFilterActive(FrontendFilter.ToSharedRef(), false);
							ExecuteOnFilterChanged = true;
						}
					}
				}
			}
		}

		// Disable all backend filters if it was determined that the combined backend filter hides any of the assets
		if (bDisableAllBackendFilters)
		{
			for(const TSharedPtr<SAssetFilter> AssetFilter : AssetFilters)
			{
				if(AssetFilter.IsValid())
				{
					FARFilter BackendFilter = AssetFilter->GetBackendFilter();
					if (!BackendFilter.IsEmpty())
					{
						AssetFilter->SetEnabled(false, false);
						ExecuteOnFilterChanged = true;
					}
				}
			}
		}

		if (ExecuteOnFilterChanged)
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
	for ( const TSharedPtr<SFilter> Filter : this->Filters )
	{
		const FString FilterName = Filter->GetFilterName();

		// If it is a FrontendFilter
		if ( Filter->GetFrontendFilter().IsValid() )
		{
			if ( ActiveFrontendFilterString.Len() > 0 )
			{
				ActiveFrontendFilterString += TEXT(",");
			}
			ActiveFrontendFilterString += FilterName;
		
			if ( Filter->IsEnabled() )
			{
				if ( EnabledFrontendFilterString.Len() > 0 )
				{
					EnabledFrontendFilterString += TEXT(",");
				}

				EnabledFrontendFilterString += FilterName;
			}

			const TSharedPtr<FFilterBase<FAssetFilterType>>& FrontendFilter = Filter->GetFrontendFilter();
			const FString CustomSettingsString = FString::Printf(TEXT("%s.CustomSettings.%s"), *SettingsString, *FilterName);
			FrontendFilter->SaveSettings(IniFilename, IniSection, CustomSettingsString);

		}
		// Otherwise we assume it is a type filter
		else
		{
			if ( ActiveTypeFilterString.Len() > 0 )
			{
				ActiveTypeFilterString += TEXT(",");
			}
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

		for(const TSharedRef<FCustomClassFilterData> &CustomClassFilter : CustomClassFilters)
		{
			if(!this->IsClassTypeInUse(CustomClassFilter))
			{
				const FString FilterName = CustomClassFilter->GetFilterName();
				if ( TypeFilterNames.Contains(FilterName) )
				{
					TSharedRef<SFilter> NewFilter = AddAssetFilterToBar(CustomClassFilter);

					if ( EnabledTypeFilterNames.Contains(FilterName) )
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
			TSharedRef<FFilterBase<FAssetFilterType>>& FrontendFilter = *FrontendFilterIt;
			const FString& FilterName = FrontendFilter->GetName();
			if (!IsFrontendFilterInUse(FrontendFilter))
			{
				if ( FrontendFilterNames.Contains(FilterName) )
				{
					TSharedRef<SFilter> NewFilter = AddFilterToBar(FrontendFilter);

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

void SFilterList::SetFrontendFilterCheckState(const TSharedPtr<FFrontendFilter>& InFrontendFilter, ECheckBoxState CheckState)
{
	this->SetFilterCheckState(InFrontendFilter, CheckState);
}

ECheckBoxState SFilterList::GetFrontendFilterCheckState(const TSharedPtr<FFrontendFilter>& InFrontendFilter) const
{
	return this->GetFilterCheckState(InFrontendFilter);
}

bool SFilterList::IsFrontendFilterActive(const TSharedPtr<FFrontendFilter>& InFrontendFilter) const
{
	return this->IsFilterActive(InFrontendFilter);
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

void SFilterList::PopulateAddFilterMenu_Internal(UToolMenu* Menu)
{
	EAssetTypeCategories::Type MenuExpansion = EAssetTypeCategories::Basic;
	if (UContentBrowserFilterListContext* Context = Menu->FindContext<UContentBrowserFilterListContext>())
	{
		MenuExpansion = Context->MenuExpansion;
	}

	this->PopulateAddFilterMenu(Menu, AssetFilterCategories.FindChecked(EAssetTypeCategories::Basic), FOnFilterAssetType::CreateLambda([this](UClass *TestClass)
	{
		return !IsFilteredByPicker(this->InitialClassFilters, TestClass);
	}));

	Menu->AddSection("ContentBrowserFilterMiscAsset", LOCTEXT("MiscAssetsMenuHeading", "Misc Options") );
}

TSharedRef<SWidget> SFilterList::MakeAddFilterMenu()
{
	const FName FilterMenuName = "ContentBrowser.FilterMenu";
	if (!UToolMenus::Get()->IsMenuRegistered(FilterMenuName))
	{
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(FilterMenuName);
		Menu->bShouldCloseWindowAfterMenuSelection = true;
		Menu->bCloseSelfOnly = true;

		Menu->AddDynamicSection(NAME_None, FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
		{
			if (UContentBrowserFilterListContext* Context = InMenu->FindContext<UContentBrowserFilterListContext>())
			{
				if (TSharedPtr<SFilterList> FilterList = Context->FilterList.Pin())
				{
					FilterList->PopulateAddFilterMenu_Internal(InMenu);
				}
			}
		}));
	}

	UContentBrowserFilterListContext* ContentBrowserFilterListContext = NewObject<UContentBrowserFilterListContext>();
	ContentBrowserFilterListContext->FilterList = SharedThis(this);
	ContentBrowserFilterListContext->MenuExpansion = EAssetTypeCategories::Basic;
	FToolMenuContext ToolMenuContext(ContentBrowserFilterListContext);

	return UToolMenus::Get()->GenerateWidget(FilterMenuName, ToolMenuContext);
}

TSharedRef<SWidget> SFilterList::MakeAddFilterMenu(EAssetTypeCategories::Type MenuExpansion)
{
	const FName FilterMenuName = "ContentBrowser.FilterMenu";
	if (!UToolMenus::Get()->IsMenuRegistered(FilterMenuName))
	{
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(FilterMenuName);
		Menu->bShouldCloseWindowAfterMenuSelection = true;
		Menu->bCloseSelfOnly = true;

		Menu->AddDynamicSection(NAME_None, FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
		{
			if (UContentBrowserFilterListContext* Context = InMenu->FindContext<UContentBrowserFilterListContext>())
			{
				if (TSharedPtr<SFilterList> FilterList = Context->FilterList.Pin())
				{
					FilterList->PopulateAddFilterMenu_Internal(InMenu);
				}
			}
		}));
	}

	UContentBrowserFilterListContext* ContentBrowserFilterListContext = NewObject<UContentBrowserFilterListContext>();
	ContentBrowserFilterListContext->FilterList = SharedThis(this);
	ContentBrowserFilterListContext->MenuExpansion = MenuExpansion;
	FToolMenuContext ToolMenuContext(ContentBrowserFilterListContext);

	return UToolMenus::Get()->GenerateWidget(FilterMenuName, ToolMenuContext);
}

#undef LOCTEXT_NAMESPACE
