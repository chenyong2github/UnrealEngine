// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Modules/ModuleManager.h"
#include "MuV/MutableValidationSettings.h"


#define LOCTEXT_NAMESPACE "MutableSettings"

/**
 * StaticMesh editor module
 */
class FMutableValidationModule : public FDefaultModuleImpl
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	bool HandleSettingsSaved() const;

private:
	ISettingsSectionPtr SettingsSectionPtr = nullptr;
};

IMPLEMENT_MODULE(FMutableValidationModule, MutableValidation);

bool FMutableValidationModule::HandleSettingsSaved() const
{
	UMutableValidationSettings* CustomizableObjectSettings = GetMutableDefault<UMutableValidationSettings>();

	if (CustomizableObjectSettings != nullptr)
	{
		CustomizableObjectSettings->SaveConfig();
	}

    return true;
}


void FMutableValidationModule::StartupModule()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{ 		
		 SettingsSectionPtr = SettingsModule->RegisterSettings("Project", "Plugins", "MutableValidationSettings",
			LOCTEXT("MutableSettings_Setting", "Mutable Validation"),
			LOCTEXT("MutableSettings_Setting_Desc", "Mutable resources validation settings"),
			GetMutableDefault<UMutableValidationSettings>()
		);
		
		if (SettingsSectionPtr.IsValid())
        {
        	SettingsSectionPtr->OnModified().BindRaw(this, &FMutableValidationModule::HandleSettingsSaved);
        }
	}
}


void FMutableValidationModule::ShutdownModule()
{
	// Unbind OnModified delegate
	if (SettingsSectionPtr)
	{
		SettingsSectionPtr->OnModified().Unbind();
	}
	
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule != nullptr)
	{ 		
		SettingsModule->UnregisterSettings("Project", "Plugins", "MutableValidationSettings");
	}
}


#undef LOCTEXT_NAMESPACE
