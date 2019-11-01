//
// Copyright (C) Valve Corporation. All rights reserved.
//

#include "PhononPluginManager.h"
#include "SteamAudioModule.h"
#include "AudioDevice.h"

namespace SteamAudio
{
	FPhononPluginManager::FPhononPluginManager()
		: bEnvironmentInitialized(false)
		, ReverbPtr(nullptr)
		, OcclusionPtr(nullptr)
	{
	}

	FPhononPluginManager::~FPhononPluginManager()
	{
		// Perform cleanup here instead of in OnListenerShutdown, because plugins will still be active and may be using them
		if (bEnvironmentInitialized)
		{
			Environment.Shutdown();
			bEnvironmentInitialized = false;
		}
	}

	void FPhononPluginManager::InitializeEnvironment(FAudioDevice* AudioDevice, UWorld* ListenerWorld)
	{
		if (ListenerWorld->WorldType == EWorldType::Editor || bEnvironmentInitialized)
		{
			UE_LOG(LogSteamAudio, Log, TEXT("Trying to initialize environment with editor world, or environment is already initialized. Doing nothing."));
			return;
		}

		bool bIsUsingOcclusion = IsUsingSteamAudioPlugin(EAudioPlugin::OCCLUSION);
		bool bIsUsingReverb = IsUsingSteamAudioPlugin(EAudioPlugin::REVERB);

		if (bIsUsingOcclusion || bIsUsingReverb)
		{
			if (Environment.Initialize(ListenerWorld, AudioDevice))
			{
				FScopeLock EnvironmentLock(Environment.GetEnvironmentCriticalSectionHandle());

				UE_LOG(LogSteamAudio, Log, TEXT("Environment initialization successful."));

				if (bIsUsingReverb)
				{
					ReverbPtr = static_cast<FPhononReverb*>(AudioDevice->ReverbPluginInterface.Get());
					ReverbPtr->SetEnvironment(&Environment);
					ReverbPtr->CreateReverbEffect();
				}

				if (bIsUsingOcclusion)
				{
					OcclusionPtr = static_cast<FPhononOcclusion*>(AudioDevice->OcclusionInterface.Get());
					OcclusionPtr->SetEnvironment(&Environment);
				}

				bEnvironmentInitialized = true;
			}
			else
			{
				UE_LOG(LogSteamAudio, Warning, TEXT("Environment initialization unsuccessful."));
			}
		}
	}

	void FPhononPluginManager::OnListenerInitialize(FAudioDevice* AudioDevice, UWorld* ListenerWorld)
	{
		InitializeEnvironment(AudioDevice, ListenerWorld);
	}

	void FPhononPluginManager::OnListenerUpdated(FAudioDevice* AudioDevice, const int32 ViewportIndex, const FTransform& ListenerTransform, const float InDeltaSeconds)
	{
		if (!bEnvironmentInitialized)
		{
			return;
		}

		FVector Position = ListenerTransform.GetLocation();
		FVector Forward = ListenerTransform.GetUnitAxis(EAxis::X);
		FVector Up = ListenerTransform.GetUnitAxis(EAxis::Z);
		FVector Right = ListenerTransform.GetUnitAxis(EAxis::Y);

		if (OcclusionPtr)
		{
			OcclusionPtr->UpdateDirectSoundSources(Position, Forward, Up, Right);
		}

		if (ReverbPtr)
		{
			ReverbPtr->UpdateListener(Position, Forward, Up, Right);
		}
	}

	void FPhononPluginManager::OnListenerShutdown(FAudioDevice* AudioDevice)
	{
		FSteamAudioModule* Module = &FModuleManager::GetModuleChecked<FSteamAudioModule>("SteamAudio");
		if (Module != nullptr)
		{
			Module->UnregisterAudioDevice(AudioDevice);
		}
	}

	void FPhononPluginManager::OnWorldChanged(FAudioDevice* AudioDevice, UWorld* ListenerWorld)
	{
		UE_LOG(LogSteamAudio, Log, TEXT("World changed. Reinitializing environment."));

		if (bEnvironmentInitialized)
		{
			Environment.Shutdown();
			bEnvironmentInitialized = false;
		}

		InitializeEnvironment(AudioDevice, ListenerWorld);
	}

	bool FPhononPluginManager::IsUsingSteamAudioPlugin(EAudioPlugin PluginType)
	{
		FSteamAudioModule* Module = &FModuleManager::GetModuleChecked<FSteamAudioModule>("SteamAudio");

		// If we can't get the module from the module manager, then we don't have any of these plugins loaded.
		if (Module == nullptr)
		{
			return false;
		}

		FString SteamPluginName = Module->GetPluginFactory(PluginType)->GetDisplayName();
		FString CurrentPluginName = AudioPluginUtilities::GetDesiredPluginName(PluginType);
		return CurrentPluginName.Equals(SteamPluginName);
	}
}
