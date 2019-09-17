// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Containers/Queue.h"

#include "AudioModulation.h"
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

		void ActivateBus(const USoundControlBusBase& InBus, const ISoundModulatable* Sound = nullptr);
		void ActivateBusMix(const USoundControlBusMix& InBusMix, const ISoundModulatable* Sound = nullptr);
		void ActivateLFO(const USoundBusModulatorLFO& InLFO, const ISoundModulatable* Sound = nullptr);

		/**
		 * Deactivates respective type if InSoundId is invalid (default behavior).  Only deactivates type if
		 * provided a InSoundId that is releasing, type is set to AutoActivate, and sounds are no longer referencing type.
		 */
		void DeactivateBusMix(const FBusMixId InBusMixId, const ISoundModulatable* Sound = nullptr);
		void DeactivateBus(const FBusId InBusId, const ISoundModulatable* Sound = nullptr);
		void DeactivateLFO(const FLFOId InLFOId, const ISoundModulatable* Sound = nullptr);

		bool IsBusActive(const FBusId InBusId) const;
		bool IsLFOActive(const FLFOId InLFOId) const;

		void ProcessAudio(const FAudioPluginSourceInputData& InputData, FAudioPluginSourceOutputData& OutputData);
		void ProcessControls(const uint32 InSourceId, FSoundModulationControls& OutControls);
		void ProcessModulators(const float Elapsed);

		void UpdateModulator(const USoundModulatorBase& InModulator);

	private:
		float CalculateModulationValue(FModulationPatchProxy& Proxy) const;

		BusMixProxyMap ActiveBusMixes;
		BusProxyMap    ActiveBuses;
		LFOProxyMap    ActiveLFOs;

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
		ISoundModulatable* PreviewSound;
		FModulationSettingsProxy PreviewSettings;
#endif // !UE_BUILD_SHIPPING
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

		void OnInitSound(ISoundModulatable* InSound, const USoundModulationPluginSourceSettingsBase& Settings) { }
		void OnInitSource(const uint32 InSourceId, const FName& AudioComponentUserId, const uint32 NumChannels, const USoundModulationPluginSourceSettingsBase& Settings) { }
		void OnReleaseSound(ISoundModulatable* InSound) { }
		void OnReleaseSource(const uint32 InSourceId) { }

#if !UE_BUILD_SHIPPING
		bool OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream) { return false; }
		int OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation) { return Y; }
		bool OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream) { return false; }
#endif // !UE_BUILD_SHIPPING

		void ActivateBus(const USoundControlBusBase& InBus, const ISoundModulatable* Sound = nullptr) { }
		void ActivateBusMix(const USoundControlBusMix& InBusMix, const ISoundModulatable* Sound = nullptr) { }
		void ActivateLFO(const USoundBusModulatorLFO& InLFO, const ISoundModulatable* Sound = nullptr) { }
		void DeactivateBusMix(const FBusMixId BusMixId, const ISoundModulatable* Sound = nullptr) { }
		void DeactivateBus(const FBusId BusId, const ISoundModulatable* Sound = nullptr) { }
		void DeactivateLFO(const FLFOId LFOId, const ISoundModulatable* Sound = nullptr) { }

		void ProcessAudio(const FAudioPluginSourceInputData& InputData, FAudioPluginSourceOutputData& OutputData) { }
		void ProcessControls(const uint32 InSourceId, FSoundModulationControls& OutControls) { }
		void ProcessModulators(const float Elapsed) { }

		void SetBusDefault(const USoundControlBusBase& InBus, const float Value) { }
		void SetBusMin(const USoundControlBusBase& InBus, const float Value) { }
		void SetBusMax(const USoundControlBusBase& InBus, const float Value) { }
		void SetBusMixChannel(const USoundControlBusMix& InBusMix, const USoundControlBusBase& InBus, const float TargetValue) { }
		void SetLFOFrequency(const USoundBusModulatorLFO& InLFO, const float Freq) { }
	};
} // namespace AudioModulation
#endif // WITH_AUDIOMODULATION
