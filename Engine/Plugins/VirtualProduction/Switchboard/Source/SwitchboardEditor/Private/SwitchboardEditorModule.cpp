// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Editor.h"
#include "Editor/Blutility/Classes/EditorUtilityObject.h"
#include "EditorUtilitySubsystem.h"
#include "ISettingsModule.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "SwitchboardEditorSettings.h"
#include "SwitchboardMenuEntry.h"
#include "Misc/CoreDelegates.h"
#include "Editor.h"


#define LOCTEXT_NAMESPACE "SwitchboardEditorModule"


FDelegateHandle DeferredStartDelegateHandle;

class FSwitchboardEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FSwitchboardMenuEntry::Register();

		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->RegisterSettings("Project", "Plugins", "Switchboard",
				LOCTEXT("SettingsName", "Switchboard"),
				LOCTEXT("Description", "Configure the Switchboard launcher."),
				GetMutableDefault<USwitchboardEditorSettings>()
			);
		}

		DeferredStartDelegateHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FSwitchboardEditorModule::RunDefaultOSCListener);
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

	void RunDefaultOSCListener()
	{
		if (DeferredStartDelegateHandle.IsValid())
		{
			FCoreDelegates::OnEndFrame.Remove(DeferredStartDelegateHandle);
			DeferredStartDelegateHandle.Reset();
		};

		USwitchboardEditorSettings* SwitchboardEditorSettings = USwitchboardEditorSettings::GetSwitchboardEditorSettings();
		FSoftObjectPath SwitchboardOSCListener = SwitchboardEditorSettings->SwitchboardOSCListener;
		if (SwitchboardOSCListener.IsValid())
		{
			UObject* SwitchboardOSCListenerObject = SwitchboardOSCListener.TryLoad();
			if (SwitchboardOSCListenerObject && !SwitchboardOSCListenerObject->IsPendingKillOrUnreachable() && GEditor)
			{
				GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>()->TryRun(SwitchboardOSCListenerObject);
			}
		}
	}

};

IMPLEMENT_MODULE(FSwitchboardEditorModule, SwitchboardEditor);

#undef LOCTEXT_NAMESPACE
