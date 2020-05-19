// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioModulation.h"
#include "Containers/Queue.h"
#include "CoreMinimal.h"
#include "IAudioModulation.h"
#include "SoundControlBus.h"
#include "SoundControlBusMix.h"
#include "SoundControlBusMixProxy.h"
#include "SoundControlBusProxy.h"
#include "SoundModulationPatchProxy.h"
#include "SoundModulationProxy.h"
#include "SoundModulationSettings.h"
#include "SoundModulationSettingsProxy.h"
#include "SoundModulationValue.h"
#include "SoundModulatorLFO.h"
#include "SoundModulatorLFOProxy.h"
#include "Templates/Function.h"

#if WITH_AUDIOMODULATION

#if !UE_BUILD_SHIPPING
#include "AudioModulationDebugger.h"
#endif // !UE_BUILD_SHIPPING

namespace AudioModulation
{
	// Forward Declarations
	class FControlBusProxy;
	class FModulationInputProxy;
	class FModulationPatchProxy;
	class FModulationPatchRefProxy;
	class FModulationSettingsProxy;
	class FModulatorBusMixChannelProxy;

	struct FReferencedProxies
	{
		FBusMixProxyMap BusMixes;
		FBusProxyMap    Buses;
		FLFOProxyMap    LFOs;
		FPatchProxyMap	Patches;
	};

	struct FReferencedModulators
	{
		TMap<FPatchHandle, TArray<uint32>> PatchMap;
		TMap<FBusHandle, TArray<uint32>> BusMap;
		TMap<FLFOHandle, TArray<uint32>> LFOMap;
	};

	class FAudioModulationSystem
	{
	public:
		FAudioModulationSystem();

		void Initialize(const FAudioPluginInitializationParams& InitializationParams);

#if WITH_EDITOR
		void OnEditPluginSettings(const USoundModulationPluginSourceSettingsBase& Settings);
#endif // WITH_EDITOR

		void OnInitSound(ISoundModulatable& InSound, const USoundModulationPluginSourceSettingsBase& Settings);
		void OnInitSource(const uint32 InSourceId, const uint32 NumChannels, const USoundModulationPluginSourceSettingsBase& Settings);
		void OnReleaseSound(ISoundModulatable& InSound);
		void OnReleaseSource(const uint32 InSourceId);

		void ActivateBus(const USoundControlBusBase& InBus);
		void ActivateBusMix(const FModulatorBusMixSettings& InSettings);
		void ActivateBusMix(const USoundControlBusMix& InBusMix);
		void ActivateLFO(const USoundBusModulatorLFO& InLFO);

		/**
		 * Deactivates respectively typed (i.e. BusMix, Bus, etc.) object proxy if no longer referenced.
		 * If still referenced, will wait until references are finished before destroying.
		 */
		void DeactivateBus(const USoundControlBusBase& InBus);
		void DeactivateBusMix(const USoundControlBusMix& InBusMix);
		void DeactivateAllBusMixes();
		void DeactivateLFO(const USoundBusModulatorLFO& InLFO);

		bool ProcessControls(const uint32 InSourceId, FSoundModulationControls& OutControls);
		void ProcessModulators(const double InElapsed);
		void SoloBusMix(const USoundControlBusMix& InBusMix);

		Audio::FModulatorTypeId RegisterModulator(uint32 InParentId, const USoundModulatorBase& InModulatorBase);
		void RegisterModulator(uint32 InParentId, Audio::FModulatorId InModulatorId);
		bool GetModulatorValue(const Audio::FModulatorHandle& ModulatorHandle, float& OutValue) const;
		void UnregisterModulator(const Audio::FModulatorHandle& InHandle);

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
		 * Commits any changes from a mix applied to a UObject definition to mix instance if active.
		 */
		void UpdateMix(const USoundControlBusMix& InMix);

		/*
		 * Commits any changes from a modulator type applied to a UObject definition
		 * to modulator instance if active (i.e. Control Bus, Control Bus Modulator)
		 */
		void UpdateModulator(const USoundModulatorBase& InModulator);

	private:
		/* Calculates modulation value, storing it in the provided float reference and returns if value changed */
		bool CalculateModulationValue(FModulationPatchProxy& OutProxy, float& OutValue) const;

		/* Whether or not caller is in processing thread or not */
		bool IsInProcessingThread() const;

		/* Runs the provided command on the audio render thread (at the beginning of the ProcessModulators call) */
		void RunCommandOnProcessingThread(TUniqueFunction<void()> Cmd);

		template <typename THandleType, typename TModType, typename TModSettings, typename TMapType>
		bool RegisterModulator(uint32 InParentId, const USoundModulatorBase& InModulatorBase, TMapType& ProxyMap, TMap<THandleType, TArray<uint32>>& ModMap)
		{
			if (const TModType* Mod = Cast<TModType>(&InModulatorBase))
			{
				RunCommandOnProcessingThread([this, Modulator = TModSettings(*Mod), InParentId, PassedProxyMap = &ProxyMap, PassedModMap = &ModMap]()
				{
					check(PassedProxyMap);
					check(PassedModMap);

					THandleType Handle = THandleType::Create(Modulator, *PassedProxyMap, *this);
					PassedModMap->FindOrAdd(Handle).Add(InParentId);
				});
				return true;
			}

			return false;
		}

		template <typename THandleType>
		bool UnregisterModulator(THandleType PatchHandle, TMap<THandleType, TArray<uint32>>& HandleMap, const uint32 ParentId)
		{
			if (!PatchHandle.IsValid())
			{
				return false;
			}

			if (TArray<uint32>* ObjectIds = HandleMap.Find(PatchHandle))
			{
				for (int32 i = 0; i < ObjectIds->Num(); ++i)
				{
					const uint32 ObjectId = (*ObjectIds)[i];
					if (ObjectId == ParentId)
					{
						ObjectIds->RemoveAtSwap(i, 1, false /* bAllowShrinking */);
						if (ObjectIds->Num() == 0)
						{
							HandleMap.Remove(PatchHandle);
						}
						return true;
					}
				}
			}

			return false;
		}

		FReferencedProxies RefProxies;

		TSet<FBusHandle>    ManuallyActivatedBuses;
		TSet<FBusMixHandle> ManuallyActivatedBusMixes;
		TSet<FLFOHandle>    ManuallyActivatedLFOs;

		// Cache of SoundId to sound setting state at time of sound initialization.
		TMap<uint32, FModulationSettingsProxy> SoundSettings;

		// Cache of source data while sound is actively playing
		TArray<FModulationSettingsProxy> SourceSettings;

		// Command queue to be consumed on processing thread 
		TQueue<TUniqueFunction<void()>, EQueueMode::Mpsc> ProcessingThreadCommandQueue;

		// Thread modulators are processed on
		uint32 ProcessingThreadId = 0;

		// Collection of maps with modulator handles to referencing object ids used by externally managing objects
		FReferencedModulators RefModulators;

#if !UE_BUILD_SHIPPING
	public:
		bool OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream);
		int OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation);
		bool OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream);

	private:
		FAudioModulationDebugger Debugger;
#endif // !UE_BUILD_SHIPPING

		friend FControlBusProxy;
		friend FModulationInputProxy;
		friend FModulationPatchProxy;
		friend FModulationPatchRefProxy;
		friend FModulationSettingsProxy;
		friend FModulatorBusMixChannelProxy;
	};
} // namespace AudioModulation

#else // WITH_AUDIOMODULATION

namespace AudioModulation
{
	// Null implementation for compiler
	class FAudioModulationSystem
	{
	public:
		void Initialize(const FAudioPluginInitializationParams& InitializationParams) { }

#if WITH_EDITOR
		void OnEditPluginSettings(const USoundModulationPluginSourceSettingsBase& Settings) { }
		void SoloBusMix(const USoundControlBusMix& InBusMix) { }
#endif // WITH_EDITOR

		void OnInitSound(ISoundModulatable& InSound, const USoundModulationPluginSourceSettingsBase& Settings) { }
		void OnInitSource(const uint32 InSourceId, const uint32 NumChannels, const USoundModulationPluginSourceSettingsBase& Settings) { }
		void OnReleaseSound(ISoundModulatable& InSound) { }
		void OnReleaseSource(const uint32 InSourceId) { }

#if !UE_BUILD_SHIPPING
		bool OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream) { return false; }
		int OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation) { return Y; }
		bool OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream) { return false; }
#endif // !UE_BUILD_SHIPPING

		void ActivateBus(const USoundControlBusBase& InBus) { }
		void ActivateBusMix(const FModulatorBusMixSettings& InSettings) { }
		void ActivateBusMix(const USoundControlBusMix& InBusMix) { }
		void ActivateLFO(const USoundBusModulatorLFO& InLFO) { }

		void DeactivateAllBusMixes() { }
		void DeactivateBus(const USoundControlBusBase& InBus) { }
		void DeactivateBusMix(const USoundControlBusBase& InBusMix) { }
		void DeactivateLFO(const USoundBusModulatorLFO& InLFO) { }

		void SaveMixToProfile(const USoundControlBusMix& InBusMix, const int32 InProfileIndex) { }
		void LoadMixFromProfile(const int32 InProfileIndex, USoundControlBusMix& OutBusMix) { }

		bool ProcessControls(const uint32 InSourceId, FSoundModulationControls& OutControls) { return false; }
		void ProcessModulators(const double InElapsed) { }

		Audio::FModulatorTypeId RegisterModulator(uint32 InParentId, const USoundModulatorBase& InModulatorBase) { return INDEX_NONE; }
		void RegisterModulator(uint32 InParentId, Audio::FModulatorId InModulatorId) { }
		bool GetModulatorValue(const Audio::FModulatorHandle& ModulatorHandle, float& OutValue) const { return false; }
		void UnregisterModulator(const Audio::FModulatorHandle& InHandle) { }

		void UpdateMix(const USoundControlBusMix& InMix) { }
		void UpdateMix(const TArray<FSoundControlBusMixChannel>& InChannels, USoundControlBusMix& InOutMix, bool bUpdateObject = false) { }
		void UpdateMixByFilter(const FString& InAddressFilter, const TSubclassOf<USoundControlBusBase>& InClassFilter, const FSoundModulationValue& InValue, USoundControlBusMix& InOutMix, bool bUpdateObject = false) { }
		void UpdateModulator(const USoundModulatorBase& InModulator) { }
	};
} // namespace AudioModulation
#endif // WITH_AUDIOMODULATION
