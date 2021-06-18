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
FName UPlatformSettings::SimulatedEditorPlatform;
#endif

UPlatformSettings::UPlatformSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

#if WITH_EDITOR
TArray<FName> UPlatformSettings::GetKnownAndEnablePlatformIniNames()
{
	TArray<FName> Results;

	FProjectStatus ProjectStatus;
	const bool bProjectStatusIsValid = IProjectManager::Get().QueryStatusForCurrentProject(/*out*/ ProjectStatus);

	for (const auto& Pair : FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos())
	{
		const FName PlatformName = Pair.Key;
		const FDataDrivenPlatformInfo& Info = Pair.Value;

		const bool bProjectDisabledPlatform = bProjectStatusIsValid && !ProjectStatus.IsTargetPlatformSupported(PlatformName);

		const bool bEnabledForUse =
#if DDPI_HAS_EXTENDED_PLATFORMINFO_DATA
			Info.bEnabledForUse;
#else
			true;
#endif

		const bool bSupportedPlatform = !Info.bIsFakePlatform && bEnabledForUse && !bProjectDisabledPlatform;

		if (bSupportedPlatform)
		{
			Results.Add(PlatformName);
		}
	}

	return Results;
}

TArray<UPlatformSettings*> UPlatformSettings::GetAllPlatformSettings(TSubclassOf<UPlatformSettings> SettingsClass)
{
	TArray<UPlatformSettings*> Settings;
	for (FName PlatformIniName : GetKnownAndEnablePlatformIniNames())
	{
		Settings.Add(GetSettingsForPlatformInternal(SettingsClass, PlatformIniName.ToString()));
	}
	
	return Settings;
}
#endif

UPlatformSettings* UPlatformSettings::GetSettingsForPlatform(TSubclassOf<UPlatformSettings> SettingsClass)
{
	static UPlatformSettings* ThisPlatformsSettings = GetSettingsForPlatformInternal(SettingsClass, FPlatformProperties::IniPlatformName());

#if WITH_EDITOR
	if (GIsEditor && SimulatedEditorPlatform != NAME_None)
	{
		UPlatformSettings* OtherPlatformsSettings = GetSettingsForPlatformInternal(SettingsClass, SimulatedEditorPlatform.ToString());
		if (ensure(OtherPlatformsSettings))
		{
			return OtherPlatformsSettings;
		}
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
