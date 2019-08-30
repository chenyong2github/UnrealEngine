//
// Copyright (C) Google Inc. 2017. All rights reserved.
//

#include "ResonanceAudioModule.h"

#include "ResonanceAudioPluginListener.h"
#include "ResonanceAudioReverb.h"
#include "ResonanceAudioSpatialization.h"
#include "Features/IModularFeatures.h"
#include "ResonanceAudioSettings.h"

IMPLEMENT_MODULE(ResonanceAudio::FResonanceAudioModule, ResonanceAudio)

namespace ResonanceAudio
{
	void* FResonanceAudioModule::ResonanceAudioDynamicLibraryHandle = nullptr;
	UResonanceAudioSpatializationSourceSettings* FResonanceAudioModule::GlobalSpatializationSourceSettings = nullptr;

	static bool bModuleInitialized = false;

	FResonanceAudioModule::FResonanceAudioModule()
	{
	}

	FResonanceAudioModule::~FResonanceAudioModule()
	{
	}

	void FResonanceAudioModule::StartupModule()
	{
		check(bModuleInitialized == false);

		bModuleInitialized = true;

		// Register the Resonance Audio plugin factories.
		IModularFeatures::Get().RegisterModularFeature(FSpatializationPluginFactory::GetModularFeatureName(), &SpatializationPluginFactory);
		IModularFeatures::Get().RegisterModularFeature(FReverbPluginFactory::GetModularFeatureName(), &ReverbPluginFactory);

		if (!IsRunningDedicatedServer() && !GlobalSpatializationSourceSettings)
		{
			// Load the global source preset settings:
			const FSoftObjectPath GlobalPluginPresetName = GetDefault<UResonanceAudioSettings>()->GlobalSourcePreset;
			if (GlobalPluginPresetName.IsValid())
			{
				GlobalSpatializationSourceSettings = LoadObject<UResonanceAudioSpatializationSourceSettings>(nullptr, *GlobalPluginPresetName.ToString());
			}
			
			if (!GlobalSpatializationSourceSettings)
			{
				GlobalSpatializationSourceSettings = NewObject<UResonanceAudioSpatializationSourceSettings>(UResonanceAudioSpatializationSourceSettings::StaticClass(), TEXT("Default Global Resonance Spatialization Preset"));
			}

			if (GlobalSpatializationSourceSettings)
			{ 
				GlobalSpatializationSourceSettings->AddToRoot();
			}
		}
	}

	void FResonanceAudioModule::ShutdownModule()
	{
		check(bModuleInitialized == true);
		bModuleInitialized = false;
	}

	IAudioPluginFactory* FResonanceAudioModule::GetPluginFactory(EAudioPlugin PluginType)
	{
		switch (PluginType)
		{
		case EAudioPlugin::SPATIALIZATION:
			return &SpatializationPluginFactory;
			break;
		case EAudioPlugin::REVERB:
			return &ReverbPluginFactory;
			break;
		default:
			return nullptr;
		}
	}

	void FResonanceAudioModule::RegisterAudioDevice(FAudioDevice* AudioDeviceHandle)
	{
		if (!RegisteredAudioDevices.Contains(AudioDeviceHandle))
		{
			TAudioPluginListenerPtr NewResonanceAudioPluginListener = TAudioPluginListenerPtr(new FResonanceAudioPluginListener());
			AudioDeviceHandle->RegisterPluginListener(NewResonanceAudioPluginListener);
			RegisteredAudioDevices.Add(AudioDeviceHandle);
		}
	}

	void FResonanceAudioModule::UnregisterAudioDevice(FAudioDevice* AudioDeviceHandle)
	{
		RegisteredAudioDevices.Remove(AudioDeviceHandle);
	}

	UResonanceAudioSpatializationSourceSettings* FResonanceAudioModule::GetGlobalSpatializationSourceSettings()
	{
		return GlobalSpatializationSourceSettings;
	}

	TAudioSpatializationPtr FSpatializationPluginFactory::CreateNewSpatializationPlugin(FAudioDevice* OwningDevice)
	{
		// Register the audio device to the Resonance Audio module.
		FResonanceAudioModule* Module = &FModuleManager::GetModuleChecked<FResonanceAudioModule>("ResonanceAudio");
		if (Module != nullptr)
		{
			Module->RegisterAudioDevice(OwningDevice);
		}
		return TAudioSpatializationPtr(new FResonanceAudioSpatialization());
	}


	TAmbisonicsMixerPtr FSpatializationPluginFactory::CreateNewAmbisonicsMixer(FAudioDevice* OwningDevice)
	{
		return TAmbisonicsMixerPtr(new FResonanceAudioAmbisonicsMixer());
	}

	TAudioReverbPtr FReverbPluginFactory::CreateNewReverbPlugin(FAudioDevice* OwningDevice)
	{
		// Register the audio device to the Resonance Audio module.
		FResonanceAudioModule* Module = &FModuleManager::GetModuleChecked<FResonanceAudioModule>("ResonanceAudio");
		if (Module != nullptr)
		{
			Module->RegisterAudioDevice(OwningDevice);
		}
		return TAudioReverbPtr(new FResonanceAudioReverb());
	}

} // namespace ResonanceAudio
