// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioPluginUtilities.h"

#include "CoreGlobals.h"
#include "CoreMinimal.h"
#include "Features/IModularFeatures.h"
#include "Misc/ConfigCacheIni.h"


static FString DefaultModulationPluginCVar = TEXT("");
FAutoConsoleVariableRef CVarActiveModulationPlugin(
	TEXT("au.DefaultModulationPlugin"),
	DefaultModulationPluginCVar,
	TEXT("Name of default modulation plugin to load and use (overridden "
	"by platform-specific implementation name in config.\n"),
	ECVF_Default);


/************************************************************************/
/* Plugin Utilities                                                     */
/************************************************************************/
/** Platform config section for each platform's target settings. */
const TCHAR* AudioPluginUtilities::GetPlatformConfigSection(EAudioPlatform AudioPlatform)
{
	static const FString UnknownConfig = TEXT("");

	switch (AudioPlatform)
	{
		case EAudioPlatform::Windows:
		{
			static const FString WindowsConfig = TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings");
			return *WindowsConfig;
		}

		case EAudioPlatform::Mac:
		{
			static const FString MacConfig = TEXT("/Script/MacTargetPlatform.MacTargetSettings");
			return *MacConfig;
		}

		case EAudioPlatform::Linux:
		{
			static const FString LinuxConfig = TEXT("/Script/LinuxTargetPlatform.LinuxTargetSettings");
			return *LinuxConfig;
		}

		case EAudioPlatform::IOS:
		{
			static const FString IOSConfig = TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings");
			return *IOSConfig;
		}

		case EAudioPlatform::Android:
		{
			static const FString AndroidConfig = TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings");
			return *AndroidConfig;
		}

		case EAudioPlatform::XboxOne:
		{
			static const FString XBoxConfig = TEXT("/Script/XboxOnePlatformEditor.XboxOneTargetSettings");
			return *XBoxConfig;
		}

		case EAudioPlatform::Playstation4:
		{
			static const FString Playstation4Config = TEXT("/Script/PS4PlatformEditor.PS4TargetSettings");
			return *Playstation4Config;
		}

		case EAudioPlatform::Switch:
		{
			static const FString SwitchConfig = TEXT("/Script/SwitchRuntimeSettings.SwitchRuntimeSettings");
			return *SwitchConfig;
		}

		case EAudioPlatform::HTML5:
		{
			static const FString HTML5Config = TEXT("/Script/HTML5PlatformEditor.HTML5TargetSettings");
			return *HTML5Config;
		}

		case EAudioPlatform::Lumin:
		{
			static FString LuminConfig = TEXT("/Script/LuminRuntimeSettings.LuminRuntimeSettings");
			return *LuminConfig;
		}

		case EAudioPlatform::HoloLens:
			return TEXT("/Script/HoloLensRuntimeSettings.HoloLensRuntimeSettings");

		case EAudioPlatform::Unknown:
		{
			return *UnknownConfig;
		}

		default:
		{
			checkf(false, TEXT("Undefined audio platform."));
			break;
		}
	}

	return *UnknownConfig;
}

/** Get the target setting name for each platform type. */
FORCEINLINE const TCHAR* GetPluginConfigName(EAudioPlugin PluginType)
{
	switch (PluginType)
	{
		case EAudioPlugin::SPATIALIZATION:
			return TEXT("SpatializationPlugin");

		case EAudioPlugin::REVERB:
			return TEXT("ReverbPlugin");

		case EAudioPlugin::OCCLUSION:
			return TEXT("OcclusionPlugin");

		case EAudioPlugin::MODULATION:
			return TEXT("ModulationPlugin");

		default:
			checkf(false, TEXT("Undefined audio plugin type."));
			return TEXT("");
	}
}

IAudioSpatializationFactory* AudioPluginUtilities::GetDesiredSpatializationPlugin(EAudioPlatform AudioPlatform)
{
	//Get the name of the desired spatialization plugin:
	FString DesiredSpatializationPlugin = GetDesiredPluginName(EAudioPlugin::SPATIALIZATION, AudioPlatform);


	TArray<IAudioSpatializationFactory *> SpatializationPluginFactories = IModularFeatures::Get().GetModularFeatureImplementations<IAudioSpatializationFactory>(IAudioSpatializationFactory::GetModularFeatureName());

	//Iterate through all of the plugins we've discovered:
	for (IAudioSpatializationFactory* PluginFactory : SpatializationPluginFactories)
	{
		//if this plugin's name matches the name found in the platform settings, use it:
		if (PluginFactory->GetDisplayName().Equals(DesiredSpatializationPlugin))
		{
			return PluginFactory;
		}
	}

	return nullptr;
}

IAudioReverbFactory* AudioPluginUtilities::GetDesiredReverbPlugin(EAudioPlatform AudioPlatform)
{
	//Get the name of the desired Reverb plugin:
	FString DesiredReverbPlugin = GetDesiredPluginName(EAudioPlugin::REVERB, AudioPlatform);

	TArray<IAudioReverbFactory *> ReverbPluginFactories = IModularFeatures::Get().GetModularFeatureImplementations<IAudioReverbFactory>(IAudioReverbFactory::GetModularFeatureName());

	//Iterate through all of the plugins we've discovered:
	for (IAudioReverbFactory* PluginFactory : ReverbPluginFactories)
	{
		//if this plugin's name matches the name found in the platform settings, use it:
		if (PluginFactory->GetDisplayName().Equals(DesiredReverbPlugin))
		{
			return PluginFactory;
		}
	}

	return nullptr;
}

IAudioOcclusionFactory* AudioPluginUtilities::GetDesiredOcclusionPlugin(EAudioPlatform AudioPlatform)
{
	FString DesiredOcclusionPlugin = GetDesiredPluginName(EAudioPlugin::OCCLUSION, AudioPlatform);

	TArray<IAudioOcclusionFactory *> OcclusionPluginFactories = IModularFeatures::Get().GetModularFeatureImplementations<IAudioOcclusionFactory>(IAudioOcclusionFactory::GetModularFeatureName());

	//Iterate through all of the plugins we've discovered:
	for (IAudioOcclusionFactory* PluginFactory : OcclusionPluginFactories)
	{
		//if this plugin's name matches the name found in the platform settings, use it:
		if (PluginFactory->GetDisplayName().Equals(DesiredOcclusionPlugin))
		{
			return PluginFactory;
		}
	}

	return nullptr;
}

IAudioModulationFactory* AudioPluginUtilities::GetDesiredModulationPlugin(EAudioPlatform AudioPlatform)
{
	const FName& PlatformPluginName = FName(*GetDesiredPluginName(EAudioPlugin::MODULATION, AudioPlatform));
	const FName& PluginName = PlatformPluginName == NAME_None ? GetDefaultModulationPluginName() : PlatformPluginName;
	const FName& FeatureName = IAudioModulationFactory::GetModularFeatureName();

	TArray<IAudioModulationFactory*> Factories = IModularFeatures::Get().GetModularFeatureImplementations<IAudioModulationFactory>(FeatureName);
	for (IAudioModulationFactory* Factory : Factories)
	{
		//if this plugin's name matches the name found in the platform settings, use it:
		if (Factory->GetDisplayName() == PluginName)
		{
			return Factory;
		}
	}

	return nullptr;
}

FString AudioPluginUtilities::GetDesiredPluginName(EAudioPlugin PluginType, EAudioPlatform AudioPlatform)
{
	FString PluginName;
	GConfig->GetString(GetPlatformConfigSection(AudioPlatform), GetPluginConfigName(PluginType), PluginName, GEngineIni);
	return PluginName;
}

const FName& AudioPluginUtilities::GetDefaultModulationPluginName()
{
	static FName DefaultModulationPluginName(TEXT("DefaultModulationPlugin"));

	if (!DefaultModulationPluginCVar.IsEmpty())
	{
		DefaultModulationPluginName = FName(*DefaultModulationPluginCVar);
	}

	return DefaultModulationPluginName;
}