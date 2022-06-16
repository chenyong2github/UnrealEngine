// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightCardEditorModule.h"

#include "DisplayClusterLightCardEditorCommands.h"
#include "SDisplayClusterLightCardEditor.h"
#include "Settings/DisplayClusterLightCardEditorSettings.h"

#include "ISettingsModule.h"

#define LOCTEXT_NAMESPACE "DisplayClusterLightCardEditor"

void FDisplayClusterLightCardEditorModule::StartupModule()
{
	RegisterTabSpawners();
	RegisterSettings();

	FDisplayClusterLightCardEditorCommands::Register();
}

void FDisplayClusterLightCardEditorModule::ShutdownModule()
{
	UnregisterTabSpawners();
	UnregisterSettings();

	FDisplayClusterLightCardEditorCommands::Unregister();
}

void FDisplayClusterLightCardEditorModule::RegisterTabSpawners()
{
	SDisplayClusterLightCardEditor::RegisterTabSpawner();
}

void FDisplayClusterLightCardEditorModule::UnregisterTabSpawners()
{
	SDisplayClusterLightCardEditor::UnregisterTabSpawner();
}

void FDisplayClusterLightCardEditorModule::RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "nDisplayLightCardEditor",
			LOCTEXT("nDisplayLightCardEditorName", "nDisplay Light Card Editor"),
			LOCTEXT("nDisplayLightCardEditorDescription", "Configure settings for the nDisplay Light Card Editor."),
			GetMutableDefault<UDisplayClusterLightCardEditorProjectSettings>());
	}
}

void FDisplayClusterLightCardEditorModule::UnregisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "nDisplayLightCardEditor");
	}
}

IMPLEMENT_MODULE(FDisplayClusterLightCardEditorModule, DisplayClusterLightCardEditor);

#undef LOCTEXT_NAMESPACE
