// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/SLibraryViewModel.h"
#include "Library/SLibraryView.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "WidgetBlueprint.h"
#include "Editor.h"
#include "FrontendFilters.h"
#include "SAssetView.h"

#if WITH_EDITOR
	#include "EditorStyleSet.h"
#endif // WITH_EDITOR

#include "DragDrop/WidgetTemplateDragDropOp.h"

#include "Templates/WidgetTemplateClass.h"
#include "Templates/WidgetTemplateBlueprintClass.h"

#include "AssetRegistryModule.h"
#include "WidgetBlueprintEditorUtils.h"

#include "Settings/ContentBrowserSettings.h"
#include "Settings/WidgetDesignerSettings.h"
#include "UMGEditorProjectSettings.h"
#include "WidgetPaletteFavorites.h"

#define LOCTEXT_NAMESPACE "UMG"

FLibraryViewModel::FLibraryViewModel(TSharedPtr<FWidgetBlueprintEditor> InBlueprintEditor)
	: bRebuildRequested(true)
{
	BlueprintEditor = InBlueprintEditor;

	FavoriteHeader = MakeShareable(new FWidgetHeaderViewModel());
	FavoriteHeader->GroupName = LOCTEXT("Favorites", "Favorites");
}

void FLibraryViewModel::RegisterToEvents()
{
	// Register for events that can trigger a Library rebuild
	GEditor->OnBlueprintReinstanced().AddRaw(this, &FLibraryViewModel::OnBlueprintReinstanced);
	FEditorDelegates::OnAssetsDeleted.AddSP(this, &FLibraryViewModel::HandleOnAssetsDeleted);
	FCoreUObjectDelegates::ReloadCompleteDelegate.AddSP(this, &FLibraryViewModel::OnReloadComplete);

	// register for any objects replaced
	FCoreUObjectDelegates::OnObjectsReplaced.AddRaw(this, &FLibraryViewModel::OnObjectsReplaced);

	// Register for favorite list update to handle the case where a favorite is added in another window of the UMG Designer
	UWidgetPaletteFavorites* Favorites = GetDefault<UWidgetDesignerSettings>()->Favorites;
	Favorites->OnFavoritesUpdated.AddSP(this, &FLibraryViewModel::OnFavoritesUpdated);
}

FLibraryViewModel::~FLibraryViewModel()
{
	GEditor->OnBlueprintReinstanced().RemoveAll(this);
	FEditorDelegates::OnAssetsDeleted.RemoveAll(this);
	FCoreUObjectDelegates::ReloadCompleteDelegate.RemoveAll(this);
	FCoreUObjectDelegates::OnObjectsReplaced.RemoveAll(this);

	UWidgetPaletteFavorites* Favorites = GetDefault<UWidgetDesignerSettings>()->Favorites;
	Favorites->OnFavoritesUpdated.RemoveAll(this);
}

void FLibraryViewModel::AddToFavorites(const FWidgetTemplateViewModel* WidgetTemplateViewModel)
{
	UWidgetPaletteFavorites* Favorites = GetDefault<UWidgetDesignerSettings>()->Favorites;
	Favorites->Add(WidgetTemplateViewModel->GetName().ToString());
}

void FLibraryViewModel::RemoveFromFavorites(const FWidgetTemplateViewModel* WidgetTemplateViewModel)
{
	UWidgetPaletteFavorites* Favorites = GetDefault<UWidgetDesignerSettings>()->Favorites;
	Favorites->Remove(WidgetTemplateViewModel->GetName().ToString());
}

void FLibraryViewModel::Update()
{
	if (bRebuildRequested)
	{
		OnUpdating.Broadcast();
		BuildWidgetList();
		bRebuildRequested = false;
		OnUpdated.Broadcast();
	}
}


UWidgetBlueprint* FLibraryViewModel::GetBlueprint() const
{
	if (BlueprintEditor.IsValid())
	{
		UBlueprint* BP = BlueprintEditor.Pin()->GetBlueprintObj();
		return Cast<UWidgetBlueprint>(BP);
	}

	return NULL;
}

void FLibraryViewModel::BuildWidgetList()
{
	// Clear the current list of view models and categories
	WidgetViewModels.Reset();
	WidgetTemplateCategories.Reset();

	// Generate a list of templates
	BuildClassWidgetList();

	// Clear the Favorite section
	bool bHasFavorites = FavoriteHeader->Children.Num() != 0;
	FavoriteHeader->Children.Reset();
	
	// Copy of the list of favorites to be able to do some cleanup in the real list
	UWidgetPaletteFavorites* FavoritesLibrary = GetDefault<UWidgetDesignerSettings>()->Favorites;
	TArray<FString> FavoritesList = FavoritesLibrary->GetFavorites();

	// For each entry in the category create a view model for the widget template
	for ( auto& Entry : WidgetTemplateCategories )
	{
		TSharedPtr<FWidgetHeaderViewModel> Header = MakeShareable(new FWidgetHeaderViewModel());
		Header->GroupName = FText::FromString(Entry.Key);


		TSharedPtr<FWidgetTemplateListViewModel> TemplateViewModel = MakeShareable(new FWidgetTemplateListViewModel());
		TemplateViewModel->Templates = Entry.Value;
		Header->Children.Add(TemplateViewModel);

		//for ( auto& Template : Entry.Value )
		//{
		//	TSharedPtr<FWidgetTemplateViewModel> TemplateViewModel = MakeShareable(new FWidgetTemplateViewModel());
		//	TemplateViewModel->Template = Template;
		//	TemplateViewModel->FavortiesViewModel = this;
		//	Header->Children.Add(TemplateViewModel);

		//	// If it's a favorite, we also add it to the Favorite section
		//	int32 index = FavoritesList.Find(Template->Name.ToString());
		//	if (index != INDEX_NONE)
		//	{
		//		TemplateViewModel->SetFavorite();

		//		// We have to create a second copy of the ViewModel for the treeview has it doesn't support to have the same element twice.
		//		TSharedPtr<FWidgetTemplateViewModel> FavoriteTemplateViewModel = MakeShareable(new FWidgetTemplateViewModel());
		//		FavoriteTemplateViewModel->Template = Template;
		//		FavoriteTemplateViewModel->FavortiesViewModel = this;
		//		FavoriteTemplateViewModel->SetFavorite();

		//		FavoriteHeader->Children.Add(FavoriteTemplateViewModel);

		//		// Remove the favorite from the temporary list
		//		FavoritesList.RemoveAt(index);
		//	}

		//}

		Header->Children.Sort([] (TSharedPtr<FWidgetViewModel> L, TSharedPtr<FWidgetViewModel> R) { return R->GetName().CompareTo(L->GetName()) > 0; });

		WidgetViewModels.Add(Header);
	}	

	// Remove all Favorites that may be left in the list.Typically happening when the list of favorite contains widget that were deleted since the last opening.
	for (const FString& favoriteName : FavoritesList)
	{
		FavoritesLibrary->Remove(favoriteName);
	}

	// Sort the view models by name
	WidgetViewModels.Sort([] (TSharedPtr<FWidgetViewModel> L, TSharedPtr<FWidgetViewModel> R) { return R->GetName().CompareTo(L->GetName()) > 0; });

	// Add the Favorite section at the top
	if (FavoriteHeader->Children.Num() != 0)
	{
		// We force expansion of the favorite header when we add favorites for the first time.
		FavoriteHeader->SetForceExpansion(!bHasFavorites);
		FavoriteHeader->Children.Sort([](TSharedPtr<FWidgetViewModel> L, TSharedPtr<FWidgetViewModel> R) { return R->GetName().CompareTo(L->GetName()) > 0; });
		WidgetViewModels.Insert(FavoriteHeader, 0);
	}
	
	// Take the Advanced Section, and put it at the end.
	TSharedPtr<FWidgetViewModel>* advancedSectionPtr = WidgetViewModels.FindByPredicate([](TSharedPtr<FWidgetViewModel> widget) {return widget->GetName().CompareTo(LOCTEXT("Advanced", "Advanced")) == 0; });
	if (advancedSectionPtr)
	{
		TSharedPtr<FWidgetViewModel> advancedSection = *advancedSectionPtr;
		WidgetViewModels.Remove(advancedSection);
		WidgetViewModels.Push(advancedSection);
	}
}

void FLibraryViewModel::BuildClassWidgetList()
{
	static const FName DevelopmentStatusKey(TEXT("DevelopmentStatus"));

	TMap<FName, TSubclassOf<UUserWidget>> LoadedWidgetBlueprintClassesByName;

	auto ActiveWidgetBlueprintClass = GetBlueprint()->GeneratedClass;
	FName ActiveWidgetBlueprintClassName = ActiveWidgetBlueprintClass->GetFName();

	TArray<FSoftClassPath> WidgetClassesToHide = GetDefault<UUMGEditorProjectSettings>()->WidgetClassesToHide;

	// Locate all UWidget classes from code and loaded widget BPs
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* WidgetClass = *ClassIt;

		if (!FWidgetBlueprintEditorUtils::IsUsableWidgetClass(WidgetClass))
		{
			continue;
		}

		if (WidgetClass->HasAnyClassFlags(CLASS_HideDropDown))
		{
			continue;
		}

		// Initialize AssetData for checking PackagePath
		FAssetData WidgetAssetData = FAssetData(WidgetClass);

		// Excludes engine content if user sets it to false
		if (!GetDefault<UContentBrowserSettings>()->GetDisplayEngineFolder() || !GetDefault<UUMGEditorProjectSettings>()->bShowWidgetsFromEngineContent)
		{
			if (WidgetAssetData.PackagePath.ToString().Find(TEXT("/Engine")) == 0)
			{
				continue;
			}
		}

		// Excludes developer content if user sets it to false
		if (!GetDefault<UContentBrowserSettings>()->GetDisplayDevelopersFolder() || !GetDefault<UUMGEditorProjectSettings>()->bShowWidgetsFromDeveloperContent)
		{
			if (WidgetAssetData.PackagePath.ToString().Find(TEXT("/Game/Developers")) == 0)
			{
				continue;
			}
		}

		// Excludes this widget if it is on the hide list
		bool bIsOnList = false;
		for (FSoftClassPath Widget : WidgetClassesToHide)
		{
			if (WidgetAssetData.ObjectPath.ToString().Find(Widget.ToString()) == 0)
			{
				bIsOnList = true;
				break;
			}
		}
		if (bIsOnList)
		{
			continue;
		}

		const bool bIsSameClass = WidgetClass->GetFName() == ActiveWidgetBlueprintClassName;

		// Check that the asset that generated this class is valid (necessary b/c of a larger issue wherein force delete does not wipe the generated class object)
		if ( bIsSameClass )
		{
			continue;
		}

		if (WidgetClass->IsChildOf(UUserWidget::StaticClass()))
		{
			if ( WidgetClass->ClassGeneratedBy )
			{
				// Track the widget blueprint classes that are already loaded
				LoadedWidgetBlueprintClassesByName.Add(WidgetClass->ClassGeneratedBy->GetFName()) = WidgetClass;
			}
		}
		else
		{
			TSharedPtr<FWidgetTemplateClass> Template = MakeShareable(new FWidgetTemplateClass(WidgetClass));

			AddWidgetTemplate(Template);
		}

		//TODO UMG does not prevent deep nested circular references
	}

	// Locate all widget BP assets (include unloaded)
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> AllBPsAssetData;
	AssetRegistryModule.Get().GetAssetsByClass(UBlueprint::StaticClass()->GetFName(), AllBPsAssetData, true);

	for (FAssetData& BPAssetData : AllBPsAssetData)
	{
		// Blueprints get the class type actions for their parent native class - this avoids us having to load the blueprint
		UClass* ParentClass = nullptr;
		FString ParentClassName;
		if (!BPAssetData.GetTagValue(FBlueprintTags::NativeParentClassPath, ParentClassName))
		{
			BPAssetData.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassName);
		}
		if (!ParentClassName.IsEmpty())
		{
			UObject* Outer = nullptr;
			ResolveName(Outer, ParentClassName, false, false);
			ParentClass = FindObject<UClass>(ANY_PACKAGE, *ParentClassName);
			// UUserWidgets have their own loading section, and we don't want to process any blueprints that don't have UWidget parents
			if (!ParentClass->IsChildOf(UWidget::StaticClass()) || ParentClass->IsChildOf(UUserWidget::StaticClass()))
			{
				continue;
			}
		}

		if (!FilterAssetData(BPAssetData))
		{
			// If this object isn't currently loaded, add it to the Library view
			if (BPAssetData.ToSoftObjectPath().ResolveObject() == nullptr)
			{
				auto Template = MakeShareable(new FWidgetTemplateClass(BPAssetData, nullptr));
				AddWidgetTemplate(Template);
			}
		}
	}

	TArray<FAssetData> AllWidgetBPsAssetData;
	AssetRegistryModule.Get().GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetFName(), AllWidgetBPsAssetData, true);

	FName ActiveWidgetBlueprintName = ActiveWidgetBlueprintClass->ClassGeneratedBy->GetFName();
	for (FAssetData& WidgetBPAssetData : AllWidgetBPsAssetData)
	{
		// Excludes the blueprint you're currently in
		if (WidgetBPAssetData.AssetName == ActiveWidgetBlueprintName)
		{
			continue;
		}

		if (!FilterAssetData(WidgetBPAssetData))
		{
			// Excludes this widget if it is on the hide list
			bool bIsOnList = false;
			for (FSoftClassPath Widget : WidgetClassesToHide)
			{
				if (Widget.ToString().Find(WidgetBPAssetData.ObjectPath.ToString()) == 0)
				{
					bIsOnList = true;
					break;
				}
			}
			if (bIsOnList)
			{
				continue;
			}

			// If the blueprint generated class was found earlier, pass it to the template
			TSubclassOf<UUserWidget> WidgetBPClass = nullptr;
			auto LoadedWidgetBPClass = LoadedWidgetBlueprintClassesByName.Find(WidgetBPAssetData.AssetName);
			if (LoadedWidgetBPClass)
			{
				WidgetBPClass = *LoadedWidgetBPClass;
			}

			uint32 BPFlags = WidgetBPAssetData.GetTagValueRef<uint32>(FBlueprintTags::ClassFlags);
			if ((BPFlags & (CLASS_Abstract | CLASS_Deprecated | CLASS_HideDropDown)) == 0)
			{
				auto Template = MakeShareable(new FWidgetTemplateBlueprintClass(WidgetBPAssetData, WidgetBPClass));

				AddWidgetTemplate(Template);
			}
		}
	}
}

bool FLibraryViewModel::FilterAssetData(FAssetData &InAssetData)
{
	// Excludes engine content if user sets it to false
	if (!GetDefault<UContentBrowserSettings>()->GetDisplayEngineFolder() || !GetDefault<UUMGEditorProjectSettings>()->bShowWidgetsFromEngineContent)
	{
		if (InAssetData.PackagePath.ToString().Find(TEXT("/Engine")) == 0)
		{
			return true;
		}
	}

	// Excludes developer content if user sets it to false
	if (!GetDefault<UContentBrowserSettings>()->GetDisplayDevelopersFolder() || !GetDefault<UUMGEditorProjectSettings>()->bShowWidgetsFromDeveloperContent)
	{
		if (InAssetData.PackagePath.ToString().Find(TEXT("/Game/Developers")) == 0)
		{
			return true;
		}
	}
	return false;
}

void FLibraryViewModel::AddWidgetTemplate(TSharedPtr<FWidgetTemplate> Template)
{
	FString Category = Template->GetCategory().ToString();

	// Hide user specific categories
	TArray<FString> CategoriesToHide = GetDefault<UUMGEditorProjectSettings>()->CategoriesToHide;
	for (FString CategoryName : CategoriesToHide)
	{
		if (Category == CategoryName)
		{
			return;
		}
	}
	WidgetTemplateArray& Group = WidgetTemplateCategories.FindOrAdd(Category);
	Group.Add(Template);
}

void FLibraryViewModel::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
}

void FLibraryViewModel::OnBlueprintReinstanced()
{
	bRebuildRequested = true;
}

void FLibraryViewModel::OnFavoritesUpdated()
{
	bRebuildRequested = true;
}

void FLibraryViewModel::OnReloadComplete(EReloadCompleteReason Reason)
{
	bRebuildRequested = true;
}

void FLibraryViewModel::HandleOnAssetsDeleted(const TArray<UClass*>& DeletedAssetClasses)
{
	for (auto DeletedAssetClass : DeletedAssetClasses)
	{
		if (DeletedAssetClass->IsChildOf(UWidgetBlueprint::StaticClass()))
		{
			bRebuildRequested = true;
		}
	}
}

#undef LOCTEXT_NAMESPACE

FWidgetTemplateListViewModel::FWidgetTemplateListViewModel()
{
}

FText FWidgetTemplateListViewModel::GetName() const
{
	return FText();
}

bool FWidgetTemplateListViewModel::IsTemplate() const
{
	return false;
}

void FWidgetTemplateListViewModel::GetFilterStrings(TArray<FString>& OutStrings) const
{
}

TSharedRef<ITableRow> FWidgetTemplateListViewModel::BuildRow(const TSharedRef<STableViewBase>& OwnerTable)
{
	// @TODO: DarenC - Temp construct on demand. Move to construct earlier?
	if (!TemplatesFilter)
	{
		// Generate filter text
		bool bHasFilters = false;
		TStringBuilder<2048> FilterString; 
		for (TSharedPtr<FWidgetTemplate> Template : Templates)
		{
			if (TSharedPtr<FWidgetTemplateClass> TemplateClass = StaticCastSharedPtr<FWidgetTemplateClass>(Template))
			{
				TWeakObjectPtr<UClass> WidgetClass = TemplateClass->GetWidgetClass();
				if (WidgetClass.IsValid())
				{
					FString TemplateName = WidgetClass->GetFName().ToString();
					if (!TemplateName.IsEmpty())
					{
						TemplateName.RemoveFromEnd(TEXT("_C"));
						FilterString += bHasFilters ? "|+" : "+";
						FilterString += TemplateName;
						bHasFilters = true;
					}
				}
			}
		}

		TSharedPtr<FFrontendFilter_Text> WidgetTextFilter = MakeShared<FFrontendFilter_Text>();
		WidgetTextFilter->SetActive(true);
		WidgetTextFilter->SetIncludeClassName(true);
		WidgetTextFilter->SetRawFilterText(FText::FromString(FilterString.ToString()));

		TemplatesFilter = MakeShared<FAssetFilterCollectionType>();
		TemplatesFilter->Add(WidgetTextFilter);
	}

	if (!AssetViewPtr)
	{
		FARFilter WidgetTemplateFilter;
		WidgetTemplateFilter.ClassNames.Add(UClass::StaticClass()->GetFName());
		WidgetTemplateFilter.ClassNames.Add(UWidgetBlueprint::StaticClass()->GetFName());

		AssetViewPtr = SNew(SAssetView)
			.InitialCategoryFilter(EContentBrowserItemCategoryFilter::IncludeAssets | EContentBrowserItemCategoryFilter::IncludeClasses)
			.InitialBackendFilter(WidgetTemplateFilter)
			.FrontendFilters(TemplatesFilter)
			.ForceShowEngineContent(true)
			.ForceShowPluginContent(true)
			;
	}
	AssetViewPtr->RequestSlowFullListRefresh();

	return SNew(STableRow<TSharedPtr<FWidgetViewModel>>, OwnerTable)
		.Padding(2.0f)
		//.OnDragDetected(this, &FWidgetTemplateViewModel::OnDraggingWidgetTemplateItem)
		[
			AssetViewPtr.ToSharedRef()
		];
}
