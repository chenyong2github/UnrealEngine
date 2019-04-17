// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Palette/SPaletteViewModel.h"
#include "Palette/SPaletteView.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "WidgetBlueprint.h"
#include "Editor.h"

#if WITH_EDITOR
	#include "EditorStyleSet.h"
#endif // WITH_EDITOR

#include "DragDrop/WidgetTemplateDragDropOp.h"

#include "Templates/WidgetTemplateClass.h"
#include "Templates/WidgetTemplateBlueprintClass.h"

#include "Developer/HotReload/Public/IHotReload.h"

#include "AssetRegistryModule.h"
#include "WidgetBlueprintEditorUtils.h"

#include "Settings/ContentBrowserSettings.h"
#include "Settings/WidgetDesignerSettings.h"
#include "UMGEditorProjectSettings.h"
#include "WidgetPaletteFavorites.h"

#define LOCTEXT_NAMESPACE "UMG"

FWidgetTemplateViewModel::FWidgetTemplateViewModel()
	: PaletteViewModel(nullptr),
	bIsFavorite(false)
{
}

FText FWidgetTemplateViewModel::GetName() const
{
	return Template->Name;
}

bool FWidgetTemplateViewModel::IsTemplate() const
{
	return true;
}

void FWidgetTemplateViewModel::GetFilterStrings(TArray<FString>& OutStrings) const
{
	Template->GetFilterStrings(OutStrings);
}

TSharedRef<ITableRow> FWidgetTemplateViewModel::BuildRow(const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FWidgetViewModel>>, OwnerTable)
		.Padding(2.0f)
		.Style(FEditorStyle::Get(), "UMGEditor.PaletteItem")
		.OnDragDetected(this, &FWidgetTemplateViewModel::OnDraggingWidgetTemplateItem)
		[
			SNew(SPaletteViewItem, SharedThis(this))
			.HighlightText(PaletteViewModel, &FPaletteViewModel::GetSearchText)
		];
}

FReply FWidgetTemplateViewModel::OnDraggingWidgetTemplateItem(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Handled().BeginDragDrop(FWidgetTemplateDragDropOp::New(Template));
}

void FWidgetTemplateViewModel::AddToFavorites()
{
	bIsFavorite = true;
	PaletteViewModel->AddToFavorites(this);
}

void FWidgetTemplateViewModel::RemoveFromFavorites()
{
	bIsFavorite = false;
	PaletteViewModel->RemoveFromFavorites(this);
}

TSharedRef<ITableRow> FWidgetHeaderViewModel::BuildRow(const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FWidgetViewModel>>, OwnerTable)
		.Style(FEditorStyle::Get(), "UMGEditor.PaletteHeader")
		.Padding(2.0f)
		.ShowSelection(false)
		[
			SNew(STextBlock)
			.Text(GroupName)
			.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
			.ShadowOffset(FVector2D(1.0f, 1.0f))
		];
}

void FWidgetHeaderViewModel::GetChildren(TArray< TSharedPtr<FWidgetViewModel> >& OutChildren)
{
	for (TSharedPtr<FWidgetViewModel>& Child : Children)
	{
		OutChildren.Add(Child);
	}
}

FPaletteViewModel::FPaletteViewModel(TSharedPtr<FWidgetBlueprintEditor> InBlueprintEditor)
	: bRebuildRequested(true),
	  bRebuildFavorites(true)
{
	BlueprintEditor = InBlueprintEditor;
}

void FPaletteViewModel::RegisterToEvents()
{
	// Register for events that can trigger a palette rebuild
	GEditor->OnBlueprintReinstanced().AddRaw(this, &FPaletteViewModel::OnBlueprintReinstanced);
	FEditorDelegates::OnAssetsDeleted.AddSP(this, &FPaletteViewModel::HandleOnAssetsDeleted);
	IHotReloadModule::Get().OnHotReload().AddSP(this, &FPaletteViewModel::HandleOnHotReload);

	// register for any objects replaced
	GEditor->OnObjectsReplaced().AddRaw(this, &FPaletteViewModel::OnObjectsReplaced);
}

FPaletteViewModel::~FPaletteViewModel()
{
	GEditor->OnBlueprintReinstanced().RemoveAll(this);
	FEditorDelegates::OnAssetsDeleted.RemoveAll(this);
	IHotReloadModule::Get().OnHotReload().RemoveAll(this);
	GEditor->OnObjectsReplaced().RemoveAll(this);
}

void FPaletteViewModel::AddToFavorites(const FWidgetTemplateViewModel* WidgetTemplateViewModel)
{
	UWidgetPaletteFavorites* Favorites = GetDefault<UWidgetDesignerSettings>()->Favorites;
	Favorites->Add(WidgetTemplateViewModel->GetName().ToString());

	// Force a rebuild of the favorites list.
	bRebuildFavorites = true;
}

void FPaletteViewModel::RemoveFromFavorites(const FWidgetTemplateViewModel* WidgetTemplateViewModel)
{
	UWidgetPaletteFavorites* Favorites = GetDefault<UWidgetDesignerSettings>()->Favorites;
	Favorites->Remove(WidgetTemplateViewModel->GetName().ToString());
	
	// Force a rebuild of the favorites list.
	bRebuildFavorites = true;
}

void FPaletteViewModel::Update()
{
	if (bRebuildRequested)
	{
		bRebuildRequested = false;

		OnUpdating.Broadcast();
		BuildWidgetList();
		OnUpdated.Broadcast();

		// We need to rebuild the list of favorites to also clear widgets that where potentially removed.
		bRebuildFavorites = true;
	}

	if (bRebuildFavorites)
	{
		bRebuildFavorites = false;
		BuildWidgetFavoriteList();
		OnFavoritesUpdated.Broadcast();
	}
}


UWidgetBlueprint* FPaletteViewModel::GetBlueprint() const
{
	if (BlueprintEditor.IsValid())
	{
		UBlueprint* BP = BlueprintEditor.Pin()->GetBlueprintObj();
		return Cast<UWidgetBlueprint>(BP);
	}

	return NULL;
}

void FPaletteViewModel::BuildWidgetList()
{
	// Clear the current list of view models and categories
	WidgetViewModels.Reset();
	WidgetTemplateCategories.Reset();

	// Generate a list of templates
	BuildClassWidgetList();

	// For each entry in the category create a view model for the widget template
	for ( auto& Entry : WidgetTemplateCategories )
	{
		TSharedPtr<FWidgetHeaderViewModel> Header = MakeShareable(new FWidgetHeaderViewModel());
		Header->GroupName = FText::FromString(Entry.Key);

		for ( auto& Template : Entry.Value )
		{
			TSharedPtr<FWidgetTemplateViewModel> TemplateViewModel = MakeShareable(new FWidgetTemplateViewModel());
			TemplateViewModel->Template = Template;
			TemplateViewModel->PaletteViewModel = this;
			Header->Children.Add(TemplateViewModel);
		}

		Header->Children.Sort([] (TSharedPtr<FWidgetViewModel> L, TSharedPtr<FWidgetViewModel> R) { return R->GetName().CompareTo(L->GetName()) > 0; });

		WidgetViewModels.Add(Header);
	}

	// Sort the view models by name
	WidgetViewModels.Sort([] (TSharedPtr<FWidgetViewModel> L, TSharedPtr<FWidgetViewModel> R) { return R->GetName().CompareTo(L->GetName()) > 0; });

	// Take the Advanced Section, and put it at the end.
	TSharedPtr<FWidgetViewModel>* advancedSectionPtr = WidgetViewModels.FindByPredicate([](TSharedPtr<FWidgetViewModel> widget) {return widget->GetName().CompareTo(LOCTEXT("Advanced", "Advanced")) == 0; });
	if (advancedSectionPtr)
	{
		TSharedPtr<FWidgetViewModel> advancedSection = *advancedSectionPtr;
		WidgetViewModels.Remove(advancedSection);
		WidgetViewModels.Push(advancedSection);
	}
}

void FPaletteViewModel::BuildWidgetFavoriteList()
{
	WidgetFavoritesViewModels.Reset();
	UWidgetPaletteFavorites* FavoritesPalette = GetDefault<UWidgetDesignerSettings>()->Favorites;

	// We Iterate on a copy to be able to clean widgets that may no longer exist
	TArray<FString> FavoritesList = FavoritesPalette->GetFavorites();
	for (const FString& favoriteName : FavoritesList)
	{
		bool bWidgetFound = false;
		for (TSharedPtr < FWidgetViewModel>& widgetSection : WidgetViewModels)
		{
			TArray < TSharedPtr<FWidgetViewModel> > Children;
			widgetSection->GetChildren(Children);

			TSharedPtr<FWidgetViewModel>* widgetViewModel = Children.FindByPredicate([&](const TSharedPtr<FWidgetViewModel>& widget) { return widget->GetName().ToString().Compare(favoriteName) == 0; });
			if (widgetViewModel != nullptr)
			{
				(*widgetViewModel)->SetFavorite();

				WidgetFavoritesViewModels.Add(*widgetViewModel);
				bWidgetFound = true;
				break;
			}
		}

		// If we did not found the widget, it no longer exists. We remove it from the original list of favorites.
		if (!bWidgetFound)
		{
			FavoritesPalette->Remove(favoriteName);
		}
	}

	WidgetFavoritesViewModels.Sort([](TSharedPtr<FWidgetViewModel> L, TSharedPtr<FWidgetViewModel> R) { return R->GetName().CompareTo(L->GetName()) > 0; });
}

void FPaletteViewModel::BuildClassWidgetList()
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
			// If this object isn't currently loaded, add it to the palette view
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

			auto Template = MakeShareable(new FWidgetTemplateBlueprintClass(WidgetBPAssetData, WidgetBPClass));

			AddWidgetTemplate(Template);
		}
	}
}

bool FPaletteViewModel::FilterAssetData(FAssetData &InAssetData)
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

void FPaletteViewModel::AddWidgetTemplate(TSharedPtr<FWidgetTemplate> Template)
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

void FPaletteViewModel::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
}

void FPaletteViewModel::OnBlueprintReinstanced()
{
	bRebuildRequested = true;
}

void FPaletteViewModel::HandleOnHotReload(bool bWasTriggeredAutomatically)
{
	bRebuildRequested = true;
}

void FPaletteViewModel::HandleOnAssetsDeleted(const TArray<UClass*>& DeletedAssetClasses)
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
