// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModelingToolsEditorModeModule.h"

#include "ModelingToolsActions.h"
#include "ModelingToolsManagerActions.h"
#include "ModelingToolsEditorModeStyle.h"
#include "ModelingToolsEditorModeSettings.h"

#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Misc/CoreDelegates.h"

#define LOCTEXT_NAMESPACE "FModelingToolsEditorModeModule"

void FModelingToolsEditorModeModule::StartupModule()
{
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FModelingToolsEditorModeModule::OnPostEngineInit);
}

void FModelingToolsEditorModeModule::ShutdownModule()
{
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);

	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "ModelingMode");
	}

	FModelingToolActionCommands::UnregisterAllToolActions();
	FModelingToolsManagerCommands::Unregister();
	FModelingModeActionCommands::Unregister();

	// Unregister slate style overrides
	FModelingToolsEditorModeStyle::Shutdown();
}


void FModelingToolsEditorModeModule::OnPostEngineInit()
{
	// Register slate style overrides
	FModelingToolsEditorModeStyle::Initialize();

	FModelingToolActionCommands::RegisterAllToolActions();
	FModelingToolsManagerCommands::Register();
	FModelingModeActionCommands::Register();

	// register settings
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{
		ISettingsSectionPtr SettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "ModelingMode",
			LOCTEXT("ModelingModeSettingsName", "Modeling Mode"),
			LOCTEXT("ModelingModeSettingsDescription", "Configure the Modeling Tools Editor Mode plugin"),
			GetMutableDefault<UModelingToolsEditorModeSettings>()
		);
	}

}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FModelingToolsEditorModeModule, ModelingToolsEditorMode)