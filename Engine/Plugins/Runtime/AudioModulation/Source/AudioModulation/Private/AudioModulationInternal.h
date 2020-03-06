// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Containers/Queue.h"

#include "AudioModulation.h"
#include "IAudioExtensionPlugin.h"
#include "SoundModulationProxy.h"
#include "SoundModulatorBase.h"
#include "SoundControlBus.h"
#include "SoundControlBusMix.h"
#include "SoundModulatorLFO.h"


#if WITH_AUDIOMODULATION

#if !UE_BUILD_SHIPPING
#include "AudioModulationDebugger.h"
#endif // !UE_BUILD_SHIPPING

namespace AudioModulation
{
	class FAudioModulationImpl
	{
	public:
		FAudioModulationImpl();

		void Initialize(const FAudioPluginInitializationParams& InitializationParams);

#if WITH_EDITOR
		void OnEditPluginSettings(const USoundModulationPluginSourceSettingsBase& Settings);
#endif // WITH_EDITOR

		void OnInitSound(ISoundModulatable& InSound, const USoundModulationPluginSourceSettingsBase& Settings);
		void OnInitSource(const uint32 InSourceId, const FName& AudioComponentUserId, const uint32 NumChannels, const USoundModulationPluginSourceSettingsBase& Settings);
		void OnReleaseSound(ISoundModulatable& InSound);
		void OnReleaseSource(const uint32 InSourceId);

		void ActivateBus(const USoundControlBusBase& InBus);
		void ActivateBusMix(const USoundControlBusMix& InBusMix);
		void ActivateLFO(const USoundBusModulatorLFO& InLFO);

		float CalculateInitialVolume(const USoundModulationPluginSourceSettingsBase& InSettingsBase);

		/**
		 * Deactivates respectively typed (i.e. BusMix, Bus, etc.) object proxy if no longer referenced.
		 * If still referenced, will wait until references are finished before destroying.
		 */
		void DeactivateBus(const USoundControlBusBase& InBus);
		void DeactivateBusMix(const USoundControlBusMix& InBusMix);
		void DeactivateLFO(const USoundBusModulatorLFO& InLFO);

		bool IsBusActive(const FBusId InBusId) const;

		bool ProcessControls(const uint32 InSourceId, FSoundModulationControls& OutControls);
		void ProcessModulators(const float Elapsed);

		/* Saves mix to .ini profile for fast iterative development that does not require re-cooking a mix */
		void SaveMixToProfile(const USoundControlBusMix& InBusMix, const int32 InProfileIndex);

		/* Loads mix from .ini profile for iterative development that does not require re-cooking a mix. Returns copy
		 * of mix channel values saved in profile. */
		TArray<FSoundControlBusMixChannel> LoadMixFromProfile(const int32 InProfileIndex, USoundControlBusMix& OutBusMix);

		/*
		 * Updates mix/mix by filter, modifying the mix instance if it is active. If bInUpdateObject is true,
		 * updates UObject definition in addition to proxy.
		 */
		void UpdateMix(const TArray<FSoundControlBusMixChannel>& InChannels, USoundControlBusMix& InOutMix, bool bInUpdateObject = false);
		void UpdateMixByFilter(const FString& InAddressFilter, const TSubclassOf<USoundControlBusBase>& InClassFilter, const FSoundModulationValue& InValue, USoundControlBusMix& InOutMix, bool bInUpdateObject = false);

		/*
		 * Commits any changes from a modulator type applied to a UObject definition
		 * to modulator instance if active (i.e. Control Bus, Control Bus Mix, Control Bus Modulator)
		 */
		void UpdateModulator(const USoundModulatorBase& InModulator);

	private:
		/** Calculates modulation value and returns updated value */
		float CalculateModulationValue(FModulationPatchProxy& Proxy) const;

		/* Calculates modulation value, storing it in the provided float reference and returns if value changed */
		bool CalculateModulationValue(FModulationPatchProxy& Proxy, float& OutValue) const;

		void RunCommandOnAudioThread(TFunction<void()> Cmd);

		FReferencedProxies RefProxies;

		TSet<FBusHandle>    ManuallyActivatedBuses;
		TSet<FBusMixHandle> ManuallyActivatedBusMixes;
		TSet<FLFOHandle>    ManuallyActivatedLFOs;

		// Cache of SoundId to sound setting state at time of sound initialization.
		TMap<uint32, FModulationSettingsProxy> SoundSettings;

		// Cache of source data while sound is actively playing
		TArray<FModulationSettingsProxy> SourceSettings;

#if !UE_BUILD_SHIPPING
	public:
		bool OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream);
		int OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation);
		bool OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream);

	private:
		FAudioModulationDebugger Debugger;
#endif // !UE_BUILD_SHIPPING

		friend FModulatorBusMixChannelProxy;
	};
} // namespace AudioModulation

#else // WITH_AUDIOMODULATION

namespace AudioModulation
{
	// Null implementation for compiler
	class FAudioModulationImpl
	{
	public:
		void Initialize(const FAudioPluginInitializationParams& InitializationParams) { }

#if WITH_EDITOR
		void OnEditPluginSettings(const USoundModulationPluginSourceSettingsBase& Settings) { }
#endif // WITH_EDITOR

		void OnInitSound(ISoundModulatable& InSound, const USoundModulationPluginSourceSettingsBase& Settings) { }
		void OnInitSource(const uint32 InSourceId, const FName& AudioComponentUserId, const uint32 NumChannels, const USoundModulationPluginSourceSettingsBase& Settings) { }
		void OnReleaseSound(ISoundModulatable& InSound) { }
		void OnReleaseSource(const uint32 InSourceId) { }

#if !UE_BUILD_SHIPPING
		bool OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream) { return false; }
		int OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation) { return Y; }
		bool OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream) { return false; }
#endif // !UE_BUILD_SHIPPING

		void ActivateBus(const USoundControlBusBase& InBus) { }
		void ActivateBusMix(const USoundControlBusMix& InBusMix) { }
		void ActivateLFO(const USoundBusModulatorLFO& InLFO) { }

		float CalculateInitialVolume(const USoundModulationPluginSourceSettingsBase& InSettingsBase) { return 1.0f; }

		void DeactivateBus(const USoundControlBusBase& InBus) { }
		void DeactivateBusMix(const USoundControlBusBase& InBusMix) { }
		void DeactivateLFO(const USoundBusModulatorLFO& InLFO) { }

		void SaveMixToProfile(const USoundControlBusMix& InBusMix, const int32 InProfileIndex) { }
		void LoadMixFromProfile(const int32 InProfileIndex, USoundControlBusMix& OutBusMix) { }

		bool ProcessControls(const uint32 InSourceId, FSoundModulationControls& OutControls) { return false; }
		void ProcessModulators(const float Elapsed) { }

		void UpdateMix(const TArray<FSoundControlBusMixChannel>& InChannels, USoundControlBusMix& InOutMix, bool bUpdateObject = false) { }
		void UpdateMixByFilter(const FString& InAddressFilter, const TSubclassOf<USoundControlBusBase>& InClassFilter, const FSoundModulationValue& InValue, USoundControlBusMix& InOutMix, bool bUpdateObject = false) { }
		void UpdateModulator(const USoundModulatorBase& InModulator) { }
	};
} // namespace AudioModulation
#endif // WITH_AUDIOMODULATION
