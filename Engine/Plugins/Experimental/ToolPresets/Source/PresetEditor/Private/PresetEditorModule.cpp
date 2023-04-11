// Copyright Epic Games, Inc. All Rights Reserved.

#include "Delegates/Delegate.h"
#include "Framework/Docking/TabManager.h"
#include "IPresetEditorModule.h"
#include "Internationalization/Internationalization.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Templates/SharedPointer.h"
#include "Textures/SlateIcon.h"
#include "UObject/NameTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SPresetManager.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PresetAsset.h"
#include "UObject/UObjectGlobals.h"
#include "PackageTools.h"
#include "PresetEditorStyle.h"

#define LOCTEXT_NAMESPACE "PresetEditorModule"

const FName PresetEditorTabName("Preset");

class FPresetEditorModule : public IPresetEditorModule
{
public:

	virtual void StartupModule() override
	{
		FModuleManager::Get().LoadModuleChecked(TEXT("DataValidation"));
		FModuleManager::Get().LoadModuleChecked(TEXT("AssetTools"));
		FModuleManager::Get().LoadModuleChecked(TEXT("ContentBrowser"));
		FModuleManager::Get().LoadModuleChecked(TEXT("AssetRegistry"));

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(PresetEditorTabName, FOnSpawnTab::CreateRaw(this, &FPresetEditorModule::HandleSpawnPresetEditorTab))
			.SetDisplayName(NSLOCTEXT("FPresetModule", "PresetTabTitle", "Preset Manager"))
			.SetTooltipText(NSLOCTEXT("FPresetModule", "PresetTooltipText", "Open the Preset Manager tab."))
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Preset.TabIcon"))
			.SetAutoGenerateMenuEntry(false);

		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FPresetEditorModule::OnPostEngineInit);

		
	}
	
	virtual void ShutdownModule() override
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PresetEditorTabName);

		FPresetEditorStyle::Shutdown();
	}

	void OnPostEngineInit()
	{
		// Register slate style overrides
		FPresetEditorStyle::Initialize();

		GenerateDefaultCollection();
	}

	virtual bool SupportsDynamicReloading() override
	{
		return true;
	}

	virtual void ExecuteOpenPresetEditor() override
	{
		FGlobalTabmanager::Get()->TryInvokeTab(PresetEditorTabName);
	}	

private:
	
	/** Handles creating the project settings tab. */
	TSharedRef<SDockTab> HandleSpawnPresetEditorTab(const FSpawnTabArgs& SpawnTabArgs)
	{
		const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
		[
			SNew(SPresetManager)
		];
		return DockTab;
	}

	void GenerateDefaultCollection()
	{
		// Load necessary modules
		FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		TArray<FAssetData> AssetData;

		// Find (or create!) the desired package for this object
		FString PackageName = "/ToolPresets/Presets/_DefaultCollection";
		FString ObjectName = "_DefaultCollection";
		const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);

		// Handle fully loading packages before creating new objects.
		UPackage* Pkg = CreatePackage(*PackageName);
		TArray<UPackage*> TopLevelPackages;
		TopLevelPackages.Add(Pkg);
		UPackageTools::HandleFullyLoadingPackages(TopLevelPackages, LOCTEXT("CreateANewObject", "Create a new object"));

		// Check for an existing object
		UObject* ExistingObject = StaticFindObject(UObject::StaticClass(), Pkg, *ObjectName);

		if (!ExistingObject)
		{
			// Create object and package
			UInteractiveToolsPresetCollectionAssetFactory* MyFactory = NewObject<UInteractiveToolsPresetCollectionAssetFactory>(UInteractiveToolsPresetCollectionAssetFactory::StaticClass()); // Can omit, and a default factory will be used
			UObject* NewObject = AssetToolsModule.Get().CreateAsset(ObjectName, PackagePath, UInteractiveToolsPresetCollectionAsset::StaticClass(), MyFactory);
			UInteractiveToolsPresetCollectionAsset* NewCollection = ExactCast<UInteractiveToolsPresetCollectionAsset>(NewObject);
			NewCollection->CollectionLabel = FText::FromString("Default Collection");
			FSavePackageArgs SavePackageArgs;
			SavePackageArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::Save(Pkg, NewObject, *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), SavePackageArgs);

			// Inform asset registry
			AssetRegistry.AssetCreated(NewObject);
		}
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPresetEditorModule, PresetEditor);

