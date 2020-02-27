// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "MagicLeapARPinTypes.h"
#include "IMagicLeapARPinFeature.h"
#include "Misc/CoreDelegates.h"
#include "MagicLeapARPinSettings.h"

#if WITH_EDITOR
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#endif // WITH_EDITOR

DEFINE_LOG_CATEGORY(LogMagicLeapARPin);

#define LOCTEXT_NAMESPACE "MagicLeapARPin"

UMagicLeapARPinSaveGame::UMagicLeapARPinSaveGame()
: PinnedID()
, ComponentWorldTransform(FTransform::Identity)
, PinTransform(FTransform::Identity)
{}

class FMagicLeapARPinModule : public IModuleInterface
{
public:
	void StartupModule()
	{
#if WITH_EDITOR
	// We don't quite have control of when the "Settings" module is loaded, so we'll wait until PostEngineInit to register settings.
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FMagicLeapARPinModule::AddEditorSettings);
#endif // WITH_EDITOR
	}

	void ShutdownModule()
	{
		RemoveEditorSettings();
	}

private:
	void AddEditorSettings()
	{
#if WITH_EDITOR
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule)
		{
			SettingsModule->RegisterSettings("Project", "Plugins", "Magic Leap AR Pin",
				LOCTEXT("MagicLeapARPinSettingsName", "MagicLeapARPin Plugin"),
				LOCTEXT("MagicLeapARPinSettingsDescription", "Configure the Magic Leap AR Pin plug-in."),
				GetMutableDefault<UMagicLeapARPinSettings>()
			);
		}
#endif // WITH_EDITOR
	}

	void RemoveEditorSettings()
	{
#if WITH_EDITOR
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
		if (SettingsModule)
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "Magic Leap AR Pin");
		}
#endif // WITH_EDITOR
	}
};

IMPLEMENT_MODULE(FMagicLeapARPinModule, MagicLeapARPin);

#undef LOCTEXT_NAMESPACE
