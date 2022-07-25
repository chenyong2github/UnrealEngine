// Copyright Epic Games, Inc. All Rights Reserved.

#include "LightMixerModule.h"

#include "LightMixerObjectFilter.h"
#include "LightMixerProjectSettings.h"
#include "LightMixerStyle.h"

#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "FLightMixerEditorModule"

IMPLEMENT_MODULE(FLightMixerModule, LightMixer)

void FLightMixerModule::StartupModule()
{
	FLightMixerStyle::Initialize();

	Initialize();
}

void FLightMixerModule::ShutdownModule()
{
	FLightMixerStyle::Shutdown();

	Teardown();
}

FLightMixerModule& FLightMixerModule::Get()
{
	return FModuleManager::LoadModuleChecked< FLightMixerModule >("LightMixer");
}

void FLightMixerModule::Initialize()
{
	FObjectMixerEditorModule::Initialize();
	
	DefaultFilterClass = ULightMixerObjectFilter::StaticClass();	
}

void FLightMixerModule::SetupMenuItemVariables()
{
	TabLabel = LOCTEXT("LightMixerTabLabel", "Light Mixer");

	MenuItemName = LOCTEXT("OpenLightMixerEditorMenuItem", "Light Mixer");
	MenuItemIcon =
		FSlateIcon(FLightMixerStyle::Get().GetStyleSetName(), "LightMixer.ToolbarButton", "LightMixer.ToolbarButton.Small");
	MenuItemTooltip = LOCTEXT("OpenLightMixerEditorTooltip", "Open Light Mixer");
}

FName FLightMixerModule::GetTabSpawnerId()
{
	return "LightMixerToolkit";
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
