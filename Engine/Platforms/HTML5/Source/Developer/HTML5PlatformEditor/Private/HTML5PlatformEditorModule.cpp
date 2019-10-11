// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "HTML5TargetSettings.h"
#include "HTML5TargetSettingsCustomization.h"
#include "ISettingsModule.h"
#include "HTML5SDKSettings.h"


#define LOCTEXT_NAMESPACE "FHTML5PlatformEditorModule"


/**
 * Module for Android platform editor utilities
 */
class FHTML5PlatformEditorModule
	: public IModuleInterface
{
	// IModuleInterface interface

	virtual void StartupModule() override
	{
		// hunt down SDK like in C# land
		FString SDKPath = FPlatformMisc::GetEnvironmentVariable(TEXT("EMSDK"));
		if (SDKPath == TEXT("") || !IFileManager::Get().DirectoryExists(*SDKPath))
		{
			const TCHAR* PlatformDirName =
#if PLATFORM_WINDOWS
				TEXT("Win64");
#elif PLATFORM_MAC
				TEXT("Mac");
#elif PLATFORM_LINUX
				TEXT("Linux");
#else 
#error Unknown host platform
#endif 

			SDKPath = FPaths::EnginePlatformExtensionsDir() / TEXT("HTML5/Build/emsdk") / PlatformDirName;

			// we don't have the SDK, don't bother setting this up. 
			if (!IFileManager::Get().DirectoryExists(*SDKPath))
			{
				// try old path
				SDKPath = FPaths::EngineDir() / TEXT("Extras/ThirdPartyNotUE/emsdk") / PlatformDirName;

				// we don't have the SDK, don't bother setting this up. 
				if (!IFileManager::Get().DirectoryExists(*SDKPath))
				{
					return;
				}
			}
		}


		// register settings
		static FName PropertyEditor("PropertyEditor");
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditor);
		PropertyModule.RegisterCustomClassLayout(
			"HTML5TargetSettings",
			FOnGetDetailCustomizationInstance::CreateStatic(&FHTML5TargetSettingsCustomization::MakeInstance)
			);

		PropertyModule.NotifyCustomizationModuleChanged();

		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

		if (SettingsModule != nullptr)
		{
			SettingsModule->RegisterSettings("Project", "Platforms", "HTML5",
				LOCTEXT("TargetSettingsName", "HTML5"),
				LOCTEXT("TargetSettingsDescription", "Settings for HTML5"),
				GetMutableDefault<UHTML5TargetSettings>()
			);
		}
	}

	virtual void ShutdownModule() override
	{
		ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

		if (SettingsModule != nullptr)
		{
			SettingsModule->UnregisterSettings("Project", "Platforms", "HTML5");
//			SettingsModule->UnregisterSettings("Project", "Platforms", "HTML5SDK");
		}
	}
};


IMPLEMENT_MODULE(FHTML5PlatformEditorModule, HTML5PlatformEditor);

#undef LOCTEXT_NAMESPACE
