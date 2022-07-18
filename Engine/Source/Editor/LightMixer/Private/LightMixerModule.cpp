// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightMixerModule.h"

#include "LightMixerObjectFilter.h"
#include "LightMixerProjectSettings.h"
#include "LightMixerStyle.h"

#include "Framework/Docking/TabManager.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FLightMixerEditorModule"

IMPLEMENT_MODULE(FLightMixerModule, LightMixer)

void FLightMixerModule::StartupModule()
{
	FLightMixerStyle::Initialize();

	RegisterTabSpawner();
	RegisterProjectSettings();
	
	TabLabel = LOCTEXT("LightMixerTabLabel", "Light Mixer");
	
	DefaultFilterClass = ULightMixerObjectFilter::StaticClass();
}

void FLightMixerModule::ShutdownModule()
{
	UToolMenus::UnregisterOwner(this);

	UnregisterTabSpawner();
	UnregisterProjectSettings();

	FLightMixerStyle::Shutdown();
}

void FLightMixerModule::RegisterTabSpawner()
{
	FTabSpawnerEntry& BrowserSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		"LightMixerToolkit",
		FOnSpawnTab::CreateRaw(this, & FLightMixerModule::SpawnMainPanelTab))
			.SetIcon(FSlateIcon(FLightMixerStyle::Get().GetStyleSetName(), "LightMixer.ToolbarButton", "LightMixer.ToolbarButton.Small"))
			.SetDisplayName(LOCTEXT("OpenLightMixerEditorMenuItem", "Light Mixer"))
			.SetTooltipText(LOCTEXT("OpenLightMixerEditorTooltip", "Open Light Mixer"))
			.SetMenuType(ETabSpawnerMenuType::Enabled);

	BrowserSpawnerEntry.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());
}

void FLightMixerModule::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FObjectMixerEditorModule::ObjectMixerToolkitPanelTabId);
}

void FLightMixerModule::RegisterProjectSettings() const
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		// User Project Settings
		const TSharedPtr<ISettingsSection> ProjectSettingsSectionPtr = SettingsModule->RegisterSettings(
			"Project", "Plugins", "Light Mixer",
			LOCTEXT("LightMixerSettingsCategoryDisplayName", "Light Mixer"),
			LOCTEXT("LightMixerSettingsDescription", "Configure Light Mixer user settings"),
			GetMutableDefault<ULightMixerProjectSettings>());
	}
}

void FLightMixerModule::UnregisterProjectSettings() const
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "Light Mixer");
	}
}

#undef LOCTEXT_NAMESPACE
