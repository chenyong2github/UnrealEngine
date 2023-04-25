// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualCameraEditorModule.h"

#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Modules/ModuleManager.h"
#include "VirtualCameraUserSettings.h"

#define LOCTEXT_NAMESPACE "FVirtualCameraEditorModule"

DEFINE_LOG_CATEGORY_STATIC(LogVirtualCameraEditor, Log, All);

class FVirtualCameraEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RegisterSettings();
	}

	virtual void ShutdownModule() override
	{
		UnregisterSettings();
	}

	void RegisterSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule != nullptr)
		{
			ISettingsSectionPtr SettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "VirtualCamera",
				LOCTEXT("VirtualCameraUserSettingsName", "Virtual Camera"),
				LOCTEXT("VirtualCameraUserSettingsDescription", "Configure the Virtual Camera settings."),
				GetMutableDefault<UVirtualCameraUserSettings>());
		}
	}

	void UnregisterSettings()
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule != nullptr)
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "VirtualCamera");
		}
	}
};
#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVirtualCameraEditorModule, VirtualCameraEditor)


