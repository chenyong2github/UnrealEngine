// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "ISettingsModule.h"
#include "SwitchboardMenuEntry.h"
#include "SwitchboardEditorSettings.h"


#define LOCTEXT_NAMESPACE "SwitchboardEditorModule"


class FSwitchboardEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FSwitchboardMenuEntry::Register();

		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->RegisterSettings("Editor", "Plugins", "Switchboard",
				LOCTEXT("SettingsName", "Switchboard"),
				LOCTEXT("Description", "Configure the Switchboard launcher."),
				GetMutableDefault<USwitchboardEditorSettings>()
			);
		}
	}

	virtual void ShutdownModule() override
	{
		if (!IsRunningCommandlet() && UObjectInitialized() && !IsEngineExitRequested())
		{
			ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
			if (SettingsModule != nullptr)
			{
				SettingsModule->UnregisterSettings("Editor", "Plugins", "Switchboard");
			}
			FSwitchboardMenuEntry::Unregister();
		}
	}
};

IMPLEMENT_MODULE(FSwitchboardEditorModule, SwitchboardEditor);

#undef LOCTEXT_NAMESPACE
