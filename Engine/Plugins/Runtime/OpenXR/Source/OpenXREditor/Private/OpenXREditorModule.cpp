// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenXREditorModule.h"
#include "OpenXRAssetDirectory.h"
#include "OpenXRInputSettings.h"

#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "OpenXR"

void FOpenXREditorModule::StartupModule()
{
	FOpenXRAssetDirectory::LoadForCook();

	// register settings
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

	if (SettingsModule != nullptr)
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "OpenXR",
			LOCTEXT("OpenXRInputSettingsName", "OpenXR Input"),
			LOCTEXT("OpenXRInputSettingsDescription", "Project settings for OpenXR plugin"),
			GetMutableDefault<UOpenXRInputSettings>()
		);
	}
}

void FOpenXREditorModule::ShutdownModule()
{
	FOpenXRAssetDirectory::ReleaseAll();

	// unregister settings
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

	if (SettingsModule != nullptr)
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "OpenXR");
	}
}

IMPLEMENT_MODULE(FOpenXREditorModule, OpenXREditor);

#undef LOCTEXT_NAMESPACE
