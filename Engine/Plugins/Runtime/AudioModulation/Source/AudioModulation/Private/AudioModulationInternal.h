// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Containers/Queue.h"

#include "AudioModulation.h"
#include "SoundModulatorBase.h"
#include "SoundModulatorBus.h"
#include "SoundModulatorBusMix.h"
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
		void Initialize(const FAudioPluginInitializationParams& InitializationParams);

#if WITH_EDITOR
		void OnEditSource(const USoundModulationPluginSourceSettingsBase& Settings);
#endif // WITH_EDITOR

		void OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, const uint32 NumChannels, USoundModulationPluginSourceSettingsBase* Settings);
		void OnReleaseSource(const uint32 SourceId);

		void ActivateBus(const USoundModulatorBusBase& Bus);
		void ActivateBus(const FModulatorBusProxy& BusProxy);
		void ActivateBusMix(const USoundModulatorBusMix& BusMix, bool bReset);
		void ActivateLFO(const USoundModulatorLFO& LFO);
		void DeactivateBusMix(const BusMixId BusMixId);
		void DeactivateBus(const BusId BusId);
		void DeactivateLFO(const LFOId LFOId);

		bool IsBusActive(const BusId BusId) const;

		void ProcessAudio(const FAudioPluginSourceInputData& InputData, FAudioPluginSourceOutputData& OutputData);
		void ProcessControls(const uint32 SourceId, FSoundModulationControls& Controls);
		void ProcessModulators(const float Elapsed);

		void SetBusDefault(const USoundModulatorBusBase& Bus, const float Value);
		void SetBusRange(const USoundModulatorBusBase& Bus, const FVector2D& Range);
		void SetBusMixChannel(const USoundModulatorBusMix& BusMix, const USoundModulatorBusBase& Bus, const float TargetValue);
		void SetLFOFrequency(const USoundModulatorLFO& LFO, const float Freq);

	private:
		float CalculateModulationValue(FModulationPatchProxy& Proxy) const;
		TSet<BusId> GetReferencedBusIds() const;

		BusMixProxyMap ActiveBusMixes;
		BusProxyMap    ActiveBuses;
		LFOProxyMap    ActiveLFOs;

		TArray<FModulationSettingsProxy> SourceSettings;

#if !UE_BUILD_SHIPPING
	public:
		bool OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream);
		int OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation);
		bool OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream);

	private:
		FAudioModulationDebugger Debugger;
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
		void OnEditSource(const USoundModulationPluginSourceSettingsBase& Settings) { }
#endif // WITH_EDITOR

		void OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, const uint32 NumChannels, USoundModulationPluginSourceSettingsBase* Settings) { }
		void OnReleaseSource(const uint32 SourceId) { }

#if !UE_BUILD_SHIPPING
		bool OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream) { return false; }
		int OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation) { return Y; }
		bool OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream) { return false; }
#endif // !UE_BUILD_SHIPPING

		void ActivateBus(const USoundModulatorBusBase& Bus) { }
		void ActivateBusMix(const USoundModulatorBusMix& BusMix, bool bReset) { }
		void ActivateLFO(const USoundModulatorLFO& LFO) { }
		void DeactivateBusMix(const BusMixId BusMixId) { }
		void DeactivateBus(const BusId BusId) { }
		void DeactivateLFO(const LFOId LFOId) { }

		void ProcessAudio(const FAudioPluginSourceInputData& InputData, FAudioPluginSourceOutputData& OutputData) { }
		void ProcessControls(const uint32 SourceId, FSoundModulationControls& Controls) { }
		void ProcessModulators(const float Elapsed) { }

		void SetBusDefault(const USoundModulatorBusBase& Bus, const float Value) { }
		void SetBusMin(const USoundModulatorBusBase& Bus, const float Value) { }
		void SetBusMax(const USoundModulatorBusBase& Bus, const float Value) { }
		void SetBusMixChannel(const USoundModulatorBusMix& BusMix, const USoundModulatorBusBase& Bus, const float TargetValue) { }
		void SetLFOFrequency(const USoundModulatorLFO& LFO, const float Freq) { }
	};
} // namespace AudioModulation
#endif // WITH_AUDIOMODULATION
