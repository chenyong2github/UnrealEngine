// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/PlatformSettings.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "UObject/PropertyPortFlags.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "HAL/PlatformProperties.h"
#include "Interfaces/IProjectManager.h"
#include "ProjectDescriptor.h"

#if WITH_EDITOR
FString UPlatformSettings::SimulatedEditorPlatform;
#endif

UPlatformSettings::UPlatformSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	
}

#if WITH_EDITOR
TArray<UPlatformSettings*> UPlatformSettings::GetAllPlatformSettings(TSubclassOf<UPlatformSettings> SettingsClass)
{
	TArray<UPlatformSettings*> Settings;

	const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject();
	
	const TMap<FName, FDataDrivenPlatformInfo>& AllPlatforms = FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos();

	for (auto& PlatformKVP : AllPlatforms)
	{
		const FName PlatformIniName = PlatformKVP.Key;
		const FDataDrivenPlatformInfo& PlatformInfo = PlatformKVP.Value;

		if (PlatformInfo.bIsFakePlatform)
		{
			continue;
		}

		if (Project->TargetPlatforms.IsEmpty() || Project->TargetPlatforms.Contains(PlatformIniName))
		{
			Settings.Add(GetSettingsForPlatformInternal(SettingsClass, PlatformIniName.ToString()));
		}
	}
	
	return Settings;
}
#endif

UPlatformSettings* UPlatformSettings::GetSettingsForPlatform(TSubclassOf<UPlatformSettings> SettingsClass)
{
	static UPlatformSettings* ThisPlatformsSettings = GetSettingsForPlatformInternal(SettingsClass, FPlatformProperties::IniPlatformName());
	
#if WITH_EDITOR
	if (GIsEditor && !SimulatedEditorPlatform.IsEmpty())
	{
		return GetSettingsForPlatformInternal(SettingsClass, SimulatedEditorPlatform);
	}
#endif
	
	return ThisPlatformsSettings;
}

UPlatformSettings* UPlatformSettings::GetSettingsForPlatformInternal(TSubclassOf<UPlatformSettings> SettingsClass, FString TargetIniPlatformName)
{
	if (!ensure(SettingsClass.Get()))
	{
		return nullptr;
	}

	const FString PlatformSettingsName = SettingsClass->GetName() + TEXT("_") + TargetIniPlatformName;
	
	UPlatformSettings* PlatformSettingsForThisClass = FindObject<UPlatformSettings>(GetTransientPackage(), *PlatformSettingsName);
	if (PlatformSettingsForThisClass == nullptr)
	{
		const TMap<FName, FDataDrivenPlatformInfo>& AllPlatforms = FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos();
		if (AllPlatforms.Contains(*TargetIniPlatformName))
		{
			PlatformSettingsForThisClass = NewObject<UPlatformSettings>(GetTransientPackage(), SettingsClass, FName(*PlatformSettingsName));
			PlatformSettingsForThisClass->ConfigPlatformName = TargetIniPlatformName;
			PlatformSettingsForThisClass->AddToRoot();
			PlatformSettingsForThisClass->InitializePlatformDefaults();

			PlatformSettingsForThisClass->LoadConfig();
		}
	}
	
	return PlatformSettingsForThisClass;
}
