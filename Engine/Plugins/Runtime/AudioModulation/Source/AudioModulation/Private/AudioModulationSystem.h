// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioModulation.h"
#include "Containers/Queue.h"
#include "CoreMinimal.h"
#include "IAudioModulation.h"
#include "SoundControlBus.h"
#include "SoundControlBusMix.h"
#include "SoundControlBusMixProxy.h"
#include "SoundControlBusProxy.h"
#include "SoundModulationParameter.h"
#include "SoundModulationPatchProxy.h"
#include "SoundModulationProxy.h"
#include "SoundModulationValue.h"
#include "SoundModulationGenerator.h"
#include "SoundModulationGeneratorProxy.h"
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
	class FModulatorBusMixStageProxy;

	struct FReferencedProxies
	{
		FBusMixProxyMap BusMixes;
		FBusProxyMap Buses;
		FGeneratorProxyMap Generators;
		FPatchProxyMap Patches;
	};

	struct FReferencedModulators
	{
		TMap<FPatchHandle, TArray<uint32>> PatchMap;
		TMap<FBusHandle, TArray<uint32>> BusMap;
		TMap<FGeneratorHandle, TArray<uint32>> GeneratorMap;
	};

	class FAudioModulationSystem
	{
	public:
		void Initialize(const FAudioPluginInitializationParams& InitializationParams);

		void ActivateBus(const USoundControlBus& InBus);
		void ActivateBusMix(const FModulatorBusMixSettings& InSettings);
		void ActivateBusMix(const USoundControlBusMix& InBusMix);
		void ActivateGenerator(const USoundModulationGenerator& InGenerator);

		/**
		 * Deactivates respectively typed (i.e. BusMix, Bus, Generator, etc.) object proxy if no longer referenced.
		 * If still referenced, will wait until references are finished before destroying.
		 */
		void DeactivateBus(const USoundControlBus& InBus);
		void DeactivateBusMix(const USoundControlBusMix& InBusMix);
		void DeactivateAllBusMixes();
		void DeactivateGenerator(const USoundModulationGenerator& InGenerator);

		Audio::FModulationParameter GetParameter(FName InParamName) const;

		void ProcessModulators(const double InElapsed);
		void SoloBusMix(const USoundControlBusMix& InBusMix);

		Audio::FDeviceId GetAudioDeviceId() const;

		Audio::FModulatorTypeId RegisterModulator(Audio::FModulatorHandleId InHandleId, const USoundModulatorBase* InModulatorBase, Audio::FModulationParameter& OutParameter);
		void RegisterModulator(Audio::FModulatorHandleId InHandleId, Audio::FModulatorId InModulatorId);
		bool GetModulatorValue(const Audio::FModulatorHandle& ModulatorHandle, float& OutValue) const;
		void UnregisterModulator(const Audio::FModulatorHandle& InHandle);

		/* Saves mix to .ini profile for fast iterative development that does not require re-cooking a mix */
		void SaveMixToProfile(const USoundControlBusMix& InBusMix, const int32 InProfileIndex);

		/* Loads mix from .ini profile for iterative development that does not require re-cooking a mix. Returns copy
		 * of mix stage values saved in profile. */
		TArray<FSoundControlBusMixStage> LoadMixFromProfile(const int32 InProfileIndex, USoundControlBusMix& OutBusMix);

		/*
		 * Updates mix/mix by filter, modifying the mix instance if it is active. If bInUpdateObject is true,
		 * updates UObject definition in addition to proxy.
		 */
		void UpdateMix(const TArray<FSoundControlBusMixStage>& InStages, USoundControlBusMix& InOutMix, bool bInUpdateObject = false, float InFadeTime = -1.0f);
		void UpdateMixByFilter(const FString& InAddressFilter, const TSubclassOf<USoundModulationParameter>& InParamClassFilter, USoundModulationParameter* InParamFilter, float Value, float FadeTime, USoundControlBusMix& InOutMix, bool bInUpdateObject = false);

		/*
		 * Commits any changes from a mix applied to a UObject definition to mix instance if active.
		 */
		void UpdateMix(const USoundControlBusMix& InMix, float InFadeTime = -1.0f);

		/*
		 * Commits any changes from a modulator type applied to a UObject definition
		 * to modulator instance if active (i.e. Control Bus, Control Bus Modulator)
		 */
		void UpdateModulator(const USoundModulatorBase& InModulator);

		void OnAuditionEnd();

	private:
		/* Calculates modulation value, storing it in the provided float reference and returns if value changed */
		bool CalculateModulationValue(FModulationPatchProxy& OutProxy, float& OutValue) const;

		/* Whether or not caller is in processing thread or not */
		bool IsInProcessingThread() const;

		/* Runs the provided command on the audio render thread (at the beginning of the ProcessModulators call) */
		void RunCommandOnProcessingThread(TUniqueFunction<void()> Cmd);

		template <typename THandleType, typename TModType, typename TModSettings, typename TMapType>
		bool RegisterModulator(Audio::FModulatorHandleId InHandleId, const USoundModulatorBase* InModulatorBase, TMapType& ProxyMap, TMap<THandleType, TArray<uint32>>& ModMap)
		{
			check(InHandleId != INDEX_NONE);

			if (const TModType* Mod = Cast<TModType>(InModulatorBase))
			{
				RunCommandOnProcessingThread([this, Modulator = TModSettings(*Mod, AudioDeviceId), InHandleId, PassedProxyMap = &ProxyMap, PassedModMap = &ModMap]()
				{
					check(PassedProxyMap);
					check(PassedModMap);

					THandleType Handle = THandleType::Create(Modulator, *PassedProxyMap, *this);
					PassedModMap->FindOrAdd(Handle).Add(InHandleId);
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

		TSet<FBusHandle> ManuallyActivatedBuses;
		TSet<FBusMixHandle> ManuallyActivatedBusMixes;
		TSet<FGeneratorHandle> ManuallyActivatedGenerators;

		// Command queue to be consumed on processing thread 
		TQueue<TUniqueFunction<void()>, EQueueMode::Mpsc> ProcessingThreadCommandQueue;

		// Thread modulators are processed on
		uint32 ProcessingThreadId = 0;

		// Collection of maps with modulator handles to referencing object ids used by externally managing objects
		FReferencedModulators RefModulators;

		Audio::FDeviceId AudioDeviceId = INDEX_NONE;

#if !UE_BUILD_SHIPPING
	public:
		void SetDebugBusFilter(const FString* InFilter);
		void SetDebugMixFilter(const FString* InFilter);
		void SetDebugMatrixEnabled(bool bInIsEnabled);
		void SetDebugGeneratorsEnabled(bool bInIsEnabled);
		void SetDebugGeneratorFilter(const FString* InFilter);
		void SetDebugGeneratorTypeFilter(const FString* InFilter, bool bInIsEnabled);
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
		friend FModulatorBusMixStageProxy;
	};
} // namespace AudioModulation

#else // WITH_AUDIOMODULATION

namespace AudioModulation
{
	// Null implementation for compiler
	class FAudioModulationSystem
	{
	public:
		Audio::FModulationParameter GetParameter(FName InParamName) { return Audio::FModulationParameter(); }
		void Initialize(const FAudioPluginInitializationParams& InitializationParams) { }

#if WITH_EDITOR
		void SoloBusMix(const USoundControlBusMix& InBusMix) { }
#endif // WITH_EDITOR

		void OnAuditionEnd() { }

#if !UE_BUILD_SHIPPING
		void SetDebugBusFilter(const FString* InFilter) { }
		void SetDebugMixFilter(const FString* InFilter) { }
		void SetDebugMatrixEnabled(bool bInIsEnabled) { }
		void SetDebugGeneratorsEnabled(bool bInIsEnabled) { }
		void SetDebugGeneratorFilter(const FString* InFilter) { }
		void SetDebugGeneratorTypeFilter(const FString* InFilter, bool bInIsEnabled) { }
		bool OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream) { return false; }
		int OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation) { return Y; }
		bool OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream) { return false; }
#endif // !UE_BUILD_SHIPPING

		void ActivateBus(const USoundControlBus& InBus) { }
		void ActivateBusMix(const FModulatorBusMixSettings& InSettings) { }
		void ActivateBusMix(const USoundControlBusMix& InBusMix) { }
		void ActivateGenerator(const USoundModulationGenerator& InGenerator) { }

		void DeactivateAllBusMixes() { }
		void DeactivateBus(const USoundControlBus& InBus) { }
		void DeactivateBusMix(const USoundControlBus& InBusMix) { }
		void DeactivateGenerator(const USoundModulationGenerator& InGenerator) { }

		void SaveMixToProfile(const USoundControlBusMix& InBusMix, const int32 InProfileIndex) { }
		void LoadMixFromProfile(const int32 InProfileIndex, USoundControlBusMix& OutBusMix) { }

		void ProcessModulators(const double InElapsed) { }

		Audio::FModulatorTypeId RegisterModulator(Audio::FModulatorHandleId InHandleId, const USoundModulatorBase* InModulatorBase, Audio::FModulationParameter& OutParameter) { return INDEX_NONE; }
		void RegisterModulator(Audio::FModulatorHandleId InHandleId, Audio::FModulatorId InModulatorId) { }
		bool GetModulatorValue(const Audio::FModulatorHandle& ModulatorHandle, float& OutValue) const { return false; }
		void UnregisterModulator(const Audio::FModulatorHandle& InHandle) { }

		void UpdateMix(const USoundControlBusMix& InMix, float InFadeTime) { }
		void UpdateMix(const TArray<FSoundControlBusMixStage>& InStages, USoundControlBusMix& InOutMix, bool bUpdateObject = false) { }
		void UpdateMixByFilter(const FString& InAddressFilter, const TSubclassOf<USoundModulationParameter>& InParamClassFilter, USoundModulationParameter* InParamFilter, float InValue, float InFadeTime, USoundControlBusMix& InOutMix, bool bUpdateObject = false) { }
		void UpdateModulator(const USoundModulatorBase& InModulator) { }
	};
} // namespace AudioModulation
#endif // WITH_AUDIOMODULATION
