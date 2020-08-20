// Copyright Epic Games, Inc. All Rights Reserved.

#include "StageMonitorEditorModule.h"

#include "ISettingsModule.h"
#include "StageMonitorEditorSettings.h"
#include "StageMonitoringSettings.h"
#include "SStageMonitorPanel.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

DEFINE_LOG_CATEGORY(LogStageMonitorEditor);

#define LOCTEXT_NAMESPACE "StageMonitorEditor"


void FStageMonitorEditorModule::StartupModule()
{
	SStageMonitorPanel::RegisterNomadTabSpawner(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsMiscCategory());

	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

	SettingsModule->RegisterSettings("Project", "Plugins", "Stage Monitor",
		LOCTEXT("StageMonitoringName", "Stage Monitoring"),
		LOCTEXT("StageMonitoringDescription", "Configure the different parts of stage monitoring plugin."),
		GetMutableDefault<UStageMonitoringSettings>());

	SettingsModule->RegisterSettings("Project", "Plugins", "Stage Monitor Editor",
		LOCTEXT("StageMonitorEditorName", "Stage Monitor Editor"),
		LOCTEXT("StageMonitorEditorDescription", "Configure the editor aspects of StageMonitor plugin."),
		GetMutableDefault<UStageMonitorEditorSettings>());
}

void FStageMonitorEditorModule::ShutdownModule()
{
	if (!IsRunningCommandlet() && UObjectInitialized() && !IsEngineExitRequested())
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule != nullptr)
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "Stage Monitor");
			SettingsModule->UnregisterSettings("Project", "Plugins", "Stage Monitor Editor");
		}

		SStageMonitorPanel::UnregisterNomadTabSpawner();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FStageMonitorEditorModule, StageMonitorEditor)
