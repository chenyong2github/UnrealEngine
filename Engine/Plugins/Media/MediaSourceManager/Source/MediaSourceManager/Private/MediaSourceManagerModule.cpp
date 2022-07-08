// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaSourceManagerModule.h"

#include "MediaSourceManagerSettings.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#endif //WITH_EDITOR

#define LOCTEXT_NAMESPACE "MediaSourceManagerModule"

DEFINE_LOG_CATEGORY(LogMediaSourceManager);

/**
 * Implements the MediaSourceManager module.
 */
class FMediaSourceManagerModule
	: public IMediaSourceManagerModule
{
public:
	//~ IMediaSourceManagerModule interface
	

	//~ IModuleInterface interface

	virtual void StartupModule() override
	{
		RegisterSettings();
	}

	virtual void ShutdownModule() override
	{
		UnregisterSettings();
	}

private:

	void RegisterSettings()
	{
#if WITH_EDITOR
		if (GIsEditor)
		{
			// register settings
			ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
			if (SettingsModule != nullptr)
			{
				SettingsModule->RegisterSettings("Project", "Plugins", "MediaSourceManager",
					LOCTEXT("SettingsName", "Media Source Manager"),
					LOCTEXT("Description", "Configure the Media Source Manager."),
					GetMutableDefault<UMediaSourceManagerSettings>()
				);
			}
		}
#endif //WITH_EDITOR
	}

	void UnregisterSettings()
	{
#if WITH_EDITOR
		if (GIsEditor)
		{
			// unregister settings
			ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
			if (SettingsModule != nullptr)
			{
				SettingsModule->UnregisterSettings("Project", "Plugins", "MediaSourceManager");
			}
		}
#endif //WITH_EDITOR
	}

};

IMPLEMENT_MODULE(FMediaSourceManagerModule, MediaSourceManager);

#undef LOCTEXT_NAMESPACE
