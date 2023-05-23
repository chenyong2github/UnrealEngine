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


};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPresetEditorModule, PresetEditor);

