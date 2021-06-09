// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "DeveloperSettings.h"

#include "PlatformSettings.generated.h"

USTRUCT()
struct FPerPlatformSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Instanced, Transient, EditAnywhere, EditFixedSize, Category = Layout)
	TArray<TObjectPtr<UPlatformSettings>> Settings;
};

/**
 * The base class of any per platform settings.  The pattern for using these is as follows.
 * 
 * Step 1) Subclass UPlatformSettings, UMyPerPlatformSettings : public UPlatformSettings.
 * 
 * Step 2) For your system should already have a UDeveloperSettings that you created so that
 *         users can customize other properties for your system in the project.  On that class
 *         you need to create a property of type FPerPlatformSettings, e.g. 
 *         UPROPERTY(EditAnywhere, Category=Platform)
 *         FPerPlatformSettings PlatformOptions
 * 
 * Step 3) In your UDeveloperSettings subclasses construct, there should be a line like this,
 *         PlatformOptions.Settings = UPlatformSettings::GetAllPlatformSettings<UMyPerPlatformSettings>();
 *         This will actually ensure that you initialize the settings exposed in the editor to whatever
 *         the current platform configuration is for them.
 * 
 * Step 4) Nothing else needed.  In your system code, you will just call 
 *         UMyPerPlatformSettings* MySettings = UPlatformSettings::GetSettingsForPlatform<UMyPerPlatformSettings>()
 *         that will get you the current settings for the active platform, or the simulated platform in the editor.
 */
UCLASS(Abstract, perObjectConfig)
class DEVELOPERSETTINGS_API UPlatformSettings : public UObject
{
	GENERATED_BODY()

public:
	UPlatformSettings(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	
public:
	template <typename TPlatformSettingsClass>
	FORCEINLINE static TPlatformSettingsClass* GetSettingsForPlatform()
	{
		return Cast<TPlatformSettingsClass>(GetSettingsForPlatform(TPlatformSettingsClass::StaticClass()));
	}

	static UPlatformSettings* GetSettingsForPlatform(TSubclassOf<UPlatformSettings> SettingsClass);

#if WITH_EDITOR
	static FString GetEditorSimulatedPlatform() { return SimulatedEditorPlatform; }
	static void SetEditorSimulatedPlatform(FString PlatformIniName) { SimulatedEditorPlatform = PlatformIniName; }

	template <typename TPlatformSettingsClass>
	FORCEINLINE static TArray<UPlatformSettings*> GetAllPlatformSettings()
	{
		return GetAllPlatformSettings(TPlatformSettingsClass::StaticClass());
	}

	static TArray<UPlatformSettings*> GetAllPlatformSettings(TSubclassOf<UPlatformSettings> SettingsClass);

	template <typename TPlatformSettingsClass>
	FORCEINLINE static TPlatformSettingsClass* GetSettingsForPlatform(FString TargetIniPlatformName)
	{
		return Cast<TPlatformSettingsClass>(GetSettingsForPlatformInternal(TPlatformSettingsClass::StaticClass(), TargetIniPlatformName));
	}
#endif

	virtual void InitializePlatformDefaults() { }

	FString GetPlatformIniName() const { return ConfigPlatformName; }
	
	virtual const TCHAR* GetConfigOverridePlatform() const override
	{
		return ConfigPlatformName.IsEmpty() ? nullptr : *ConfigPlatformName;
	}

private:
	static UPlatformSettings* GetSettingsForPlatformInternal(TSubclassOf<UPlatformSettings> SettingsClass, FString TargetIniPlatformName);

	FString ConfigPlatformName;

#if WITH_EDITOR
	static FString SimulatedEditorPlatform;
#endif
};
