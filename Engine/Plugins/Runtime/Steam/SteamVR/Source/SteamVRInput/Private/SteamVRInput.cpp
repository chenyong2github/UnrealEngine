// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SteamVRInput.h"
#include "SteamVRInputSettings.h"
#include "CoreMinimal.h"
#include "Runtime/Core/Public/Misc/Paths.h"
#include "openvr.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#endif

#define LOCTEXT_NAMESPACE "FSteamVRInputModule"

DEFINE_LOG_CATEGORY_STATIC(LogSteamInput, Log, All);

#if WITH_EDITOR
void FSteamVRInputModule::AddEditorSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

	// While this should usually be true, it's not guaranteed that the settings module will be loaded in the editor.
	// UBT allows setting bBuildDeveloperTools to false while bBuildEditor can be true.
	// The former option indirectly controls loading of the "Settings" module.
	if (SettingsModule)
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "SteamVR Input",
			LOCTEXT("SteamVRInputSettingsName", "SteamVR Input"),
			LOCTEXT("SteamVRInputSettingsDescription", "Configure SteamVR input support."),
			GetMutableDefault<USteamVRInputSettings>()
		);
	}
}

void FSteamVRInputModule::RemoveEditorSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule)
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "SteamVR Input");
	}
}
#endif

void FSteamVRInputModule::StartupModule()
{
#if WITH_EDITOR
	// We don't quite have control of when the "Settings" module is loaded, so we'll wait until PostEngineInit to register settings.
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FSteamVRInputModule::AddEditorSettings);
#endif // WITH_EDITOR
}

void FSteamVRInputModule::ShutdownModule()
{
#if WITH_EDITOR
	RemoveEditorSettings();
#endif
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSteamVRInputModule, SteamVRInput)
