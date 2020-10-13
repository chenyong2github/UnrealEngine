// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioModulationSystem.h"

#if WITH_AUDIOMODULATION

#if !UE_BUILD_SHIPPING
#include "AudioModulationDebugger.h"
#endif // !UE_BUILD_SHIPPING

#include "Async/Async.h"
#include "Audio/AudioAddressPattern.h"
#include "AudioModulationLogging.h"
#include "AudioModulationProfileSerializer.h"
#include "AudioModulationSettings.h"
#include "AudioThread.h"
#include "CoreGlobals.h"
#include "Engine/Engine.h"
#include "Generators/SoundModulationLFO.h"
#include "HAL/PlatformTLS.h"
#include "IAudioModulation.h"
#include "Misc/CoreDelegates.h"
#include "SoundControlBusProxy.h"
#include "SoundControlBusMixProxy.h"
#include "SoundModulationGeneratorProxy.h"
#include "SoundModulationPatchProxy.h"
#include "SoundModulationProxy.h"
#include "UObject/UObjectIterator.h"
#include "UObject/WeakObjectPtr.h"

DECLARE_DWORD_COUNTER_STAT(TEXT("Bus Count"),	STAT_AudioModulationBusCount, STATGROUP_AudioModulation)
DECLARE_DWORD_COUNTER_STAT(TEXT("Generator Count"),	STAT_AudioModulationGeneratorCount, STATGROUP_AudioModulation)
DECLARE_DWORD_COUNTER_STAT(TEXT("Mix Count"),	STAT_AudioModulationMixCount, STATGROUP_AudioModulation)
DECLARE_DWORD_COUNTER_STAT(TEXT("Patch Count"), STAT_AudioModulationPatchCount, STATGROUP_AudioModulation)
DECLARE_DWORD_COUNTER_STAT(TEXT("Render Queue Commands Processed"), STAT_AudioModulationProcQueueCount, STATGROUP_AudioModulation)

namespace AudioModulation
{
	enum class EModulatorType : Audio::FModulatorTypeId
	{
		Patch,
		Bus,
		Generator,

		COUNT
	};

	void FAudioModulationSystem::Initialize(const FAudioPluginInitializationParams& InitializationParams)
	{
		AudioDeviceId = InitializationParams.AudioDevicePtr->DeviceID;

		// Load all parameters listed in settings and keep loaded.  Must be done on initialization as
		// soft object paths cannot be loaded on non-game threads (data from parameters is copied to a proxy
		// in 'GetParameter' via the audio thread)
		if (const UAudioModulationSettings* ModulationSettings = GetDefault<UAudioModulationSettings>())
		{
			for (const FSoftObjectPath& Path : ModulationSettings->Parameters)
			{
				if (UObject* ParamObj = Path.TryLoad())
				{
					ParamObj->AddToRoot();
				}
				else
				{
					UE_LOG(LogAudioModulation, Error, TEXT("Failed to load modulation parameter from path '%s'"), *Path.ToString());
				}
			}
		}
	}

	void FAudioModulationSystem::OnAuditionEnd()
	{
		DeactivateAllBusMixes();
	}

#if !UE_BUILD_SHIPPING
	bool FAudioModulationSystem::OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream)
	{
		check(IsInGameThread());
		return ViewportClient ? Debugger.OnPostHelp(*ViewportClient, Stream) : true;
	}

	int32 FAudioModulationSystem::OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation)
	{
		check(IsInGameThread());
		return Canvas ? Debugger.OnRenderStat(*Canvas, X, Y, Font) : Y;
	}

	bool FAudioModulationSystem::OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream)
	{
		check(IsInGameThread());
		return ViewportClient ? Debugger.OnToggleStat(*ViewportClient, Stream) : true;
	}
#endif // !UE_BUILD_SHIPPING

	void FAudioModulationSystem::ActivateBus(const USoundControlBus& InBus)
	{
		RunCommandOnProcessingThread([this, Settings = FControlBusSettings(InBus, AudioDeviceId)]()
		{
			FBusHandle BusHandle = FBusHandle::Create(Settings, RefProxies.Buses, *this);
			ManuallyActivatedBuses.Add(MoveTemp(BusHandle));
		});
	}

	void FAudioModulationSystem::ActivateBusMix(const FModulatorBusMixSettings& InSettings)
	{
		RunCommandOnProcessingThread([this, InSettings]()
		{
			FBusMixHandle BusMixHandle = FBusMixHandle::Get(InSettings.GetId(), RefProxies.BusMixes);
			if (BusMixHandle.IsValid())
			{
				BusMixHandle.FindProxy().SetEnabled(InSettings);
			}
			else
			{
				BusMixHandle = FBusMixHandle::Create(InSettings, RefProxies.BusMixes, *this);
			}

			ManuallyActivatedBusMixes.Add(MoveTemp(BusMixHandle));
		});
	}

	void FAudioModulationSystem::ActivateBusMix(const USoundControlBusMix& InBusMix)
	{
		ActivateBusMix(FModulatorBusMixSettings(InBusMix, AudioDeviceId));
	}

	void FAudioModulationSystem::ActivateGenerator(const USoundModulationGenerator& InGenerator)
	{
		RunCommandOnProcessingThread([this, Settings = FModulationGeneratorSettings(InGenerator, AudioDeviceId)]()
		{
			FGeneratorHandle GeneratorHandle = FGeneratorHandle::Create(Settings, RefProxies.Generators, *this);
			ManuallyActivatedGenerators.Add(MoveTemp(GeneratorHandle));
		});
	}

	bool FAudioModulationSystem::CalculateModulationValue(FModulationPatchProxy& OutProxy, float& OutValue) const
	{
		check(IsInProcessingThread());
		if (OutProxy.IsBypassed())
		{
			return false;
		}

		const float InitValue = OutValue;
		OutProxy.Update();
		OutValue = OutProxy.GetValue();
		return !FMath::IsNearlyEqual(InitValue, OutValue);
	}

	void FAudioModulationSystem::DeactivateBus(const USoundControlBus& InBus)
	{
		RunCommandOnProcessingThread([this, BusId = static_cast<FBusId>(InBus.GetUniqueID())]()
		{
			FBusHandle BusHandle = FBusHandle::Get(BusId, RefProxies.Buses);
			if (BusHandle.IsValid())
			{
				ManuallyActivatedBuses.Remove(BusHandle);
			}
		});
	}

	void FAudioModulationSystem::DeactivateBusMix(const USoundControlBusMix& InBusMix)
	{
		RunCommandOnProcessingThread([this, BusMixId = static_cast<FBusMixId>(InBusMix.GetUniqueID())]()
		{
			FBusMixHandle MixHandle = FBusMixHandle::Get(BusMixId, RefProxies.BusMixes);
			if (MixHandle.IsValid())
			{
				FModulatorBusMixProxy& MixProxy = MixHandle.FindProxy();
				MixProxy.SetStopping();
			}
		});
	}

	void FAudioModulationSystem::DeactivateAllBusMixes()
	{
		RunCommandOnProcessingThread([this]()
		{
			for (TPair<FBusMixId, FModulatorBusMixProxy>& Pair : RefProxies.BusMixes)
			{
				Pair.Value.SetStopping();
			}
		});
	}

	void FAudioModulationSystem::DeactivateGenerator(const USoundModulationGenerator& InGenerator)
	{
		RunCommandOnProcessingThread([this, GeneratorId = static_cast<FGeneratorId>(InGenerator.GetUniqueID())]()
		{
			FGeneratorHandle GeneratorHandle = FGeneratorHandle::Get(GeneratorId, RefProxies.Generators);
			if (GeneratorHandle.IsValid())
			{
				ManuallyActivatedGenerators.Remove(GeneratorHandle);
			}
		});
	}

#if !UE_BUILD_SHIPPING
	void FAudioModulationSystem::SetDebugBusFilter(const FString* InFilter)
	{
		Debugger.SetDebugBusFilter(InFilter);
	}

	void FAudioModulationSystem::SetDebugGeneratorsEnabled(bool bInIsEnabled)
	{
		Debugger.SetDebugGeneratorsEnabled(bInIsEnabled);
	}

	void FAudioModulationSystem::SetDebugGeneratorFilter(const FString* InFilter)
	{
		Debugger.SetDebugGeneratorFilter(InFilter);
	}

	void FAudioModulationSystem::SetDebugGeneratorTypeFilter(const FString* InFilter, bool bInEnabled)
	{
		Debugger.SetDebugGeneratorTypeFilter(InFilter, bInEnabled);
	}

	void FAudioModulationSystem::SetDebugMatrixEnabled(bool bInIsEnabled)
	{
		Debugger.SetDebugMatrixEnabled(bInIsEnabled);
	}

	void FAudioModulationSystem::SetDebugMixFilter(const FString* InNameFilter)
	{
		Debugger.SetDebugMixFilter(InNameFilter);
	}
#endif // !UE_BUILD_SHIPPING

	bool FAudioModulationSystem::GetModulatorValue(const Audio::FModulatorHandle& InModulatorHandle, float& OutValue) const
	{
		const EModulatorType ModulatorType = static_cast<EModulatorType>(InModulatorHandle.GetTypeId());

		switch (ModulatorType)
		{
			case EModulatorType::Patch:
			{
				// Direct access preferred vs through handles here as its impossible for proxies to be destroyed
				// in look-up and speed is key as this is possibly being queried often in the audio render pass.
				if (const FModulationPatchRefProxy* PatchProxy = RefProxies.Patches.Find(static_cast<FPatchId>(InModulatorHandle.GetModulatorId())))
				{
					if (!PatchProxy->IsBypassed())
					{
						OutValue = PatchProxy->GetValue();
						return true;
					}
				}
			}
			break;

			case EModulatorType::Bus:
			{
				if (const FControlBusProxy* BusProxy = RefProxies.Buses.Find(static_cast<FBusId>(InModulatorHandle.GetModulatorId())))
				{
					if (!BusProxy->IsBypassed())
					{
						OutValue = BusProxy->GetValue();
						return true;
					}
				}
			}
			break;

			case EModulatorType::Generator:
			{
				if (const FModulatorGeneratorProxy* GeneratorProxy = RefProxies.Generators.Find(static_cast<FGeneratorId>(InModulatorHandle.GetModulatorId())))
				{
					if (!GeneratorProxy->IsBypassed())
					{
						OutValue = GeneratorProxy->GetValue();
						return true;
					}
				}
			}
			break;

			default:
			{
				static_assert(static_cast<uint32>(EModulatorType::COUNT) == 3, "Possible missing modulator type coverage in switch statement");
			}
			break;
		}

		return false;
	}

	Audio::FDeviceId FAudioModulationSystem::GetAudioDeviceId() const
	{
		return AudioDeviceId;
	}

	Audio::FModulationParameter FAudioModulationSystem::GetParameter(FName InParamName) const
	{
		Audio::FModulationParameter Parameter;
		if (InParamName == FName())
		{
			return Parameter;
		}

		if (const UAudioModulationSettings* ModulationSettings = GetDefault<UAudioModulationSettings>())
		{
			for (const FSoftObjectPath& Path : ModulationSettings->Parameters)
			{
				if (const USoundModulationParameter* Param = Cast<const USoundModulationParameter>(Path.ResolveObject()))
				{
					if (Param->GetFName() == InParamName)
					{
						Parameter.ParameterName = InParamName;
						Parameter.bRequiresConversion = Param->RequiresUnitConversion();
						Parameter.MixFunction = Param->GetMixFunction();
						Parameter.UnitFunction = Param->GetUnitConversionFunction();
						Parameter.NormalizedFunction = Param->GetNormalizedConversionFunction();
						Parameter.DefaultValue = Param->GetUnitDefault();
						Parameter.MinValue = Param->GetUnitMin();
						Parameter.MaxValue = Param->GetUnitMax();

#if WITH_EDITORONLY_DATA
						Parameter.UnitDisplayName = Param->Settings.UnitDisplayName;
#endif // WITH_EDITORONLY_DATA

						return Parameter;
					}
				}
			}
		}

		UE_LOG(LogAudioModulation, Error, TEXT("Audio modulation parameter '%s' not found/loaded. Modulation may be disabled for destination referencing parameter."), *InParamName.ToString());
		return Parameter;
	}

	bool FAudioModulationSystem::IsInProcessingThread() const
	{
		return ProcessingThreadId == FPlatformTLS::GetCurrentThreadId();
	}

	void FAudioModulationSystem::ProcessModulators(const double InElapsed)
	{
		check(ProcessingThreadId == 0 || IsInProcessingThread());
		ProcessingThreadId = FPlatformTLS::GetCurrentThreadId();

		int32 CommandsProcessed = 0;
		TUniqueFunction<void()> Command;
		while (ProcessingThreadCommandQueue.Dequeue(Command))
		{
			Command();
			++CommandsProcessed;
		}

		// Update Generators (prior to bus mixing to avoid single-frame latency)
		for (TPair<FGeneratorId, FModulatorGeneratorProxy>& Pair : RefProxies.Generators)
		{
			Pair.Value.PumpCommands();
			Pair.Value.Update(InElapsed);
		}

		// Reset buses & refresh cached Generator
		for (TPair<FBusId, FControlBusProxy>& Pair : RefProxies.Buses)
		{
			Pair.Value.Reset();
			Pair.Value.MixGenerators();
		}

		// Update mix values and apply to prescribed buses.
		// Track bus mixes ready to remove
		TSet<FBusMixId> StoppedMixIds;
		for (TPair<FBusMixId, FModulatorBusMixProxy>& Pair : RefProxies.BusMixes)
		{
			const FModulatorBusMixProxy::EStatus LastStatus = Pair.Value.GetStatus();
			Pair.Value.Update(InElapsed, RefProxies.Buses);
			const FModulatorBusMixProxy::EStatus CurrentStatus = Pair.Value.GetStatus();

			switch (CurrentStatus)
			{
				case FModulatorBusMixProxy::EStatus::Enabled:
				case FModulatorBusMixProxy::EStatus::Stopping:
				break;

				case FModulatorBusMixProxy::EStatus::Stopped:
				{
					if (LastStatus != CurrentStatus)
					{
						UE_LOG(LogAudioModulation, Log, TEXT("Audio modulation mix '%s' stopped."), *Pair.Value.GetName());
					}
					StoppedMixIds.Add(Pair.Key);
				}
				break;

				default:
				{
					checkf(false, TEXT("Invalid or unsupported BusMix EStatus state advancement."));
				}
				break;
			}
		}

		// Destroy mixes that have stopped (must be done outside mix update
		// loop above to avoid destroying while iterating, which can occur
		// when update moves bus mix from 'stopping' status to 'stopped')
		for (const FBusMixId& MixId : StoppedMixIds)
		{
			FBusMixHandle MixHandle = FBusMixHandle::Get(MixId, RefProxies.BusMixes);

			// Expected to be valid given the fact that the proxy is available in the prior loop
			check(MixHandle.IsValid());

			// Expected to only have two references (one for transient 'MixHandle' and one in
			// ManuallyActivated set). Nothing else should be keeping mixes active.
			check(MixHandle.FindProxy().GetRefCount() == 2);

			ManuallyActivatedBusMixes.Remove(MoveTemp(MixHandle));
		}

		for (TPair<FPatchId, FModulationPatchRefProxy>& Pair : RefProxies.Patches)
		{
			FModulationPatchRefProxy& PatchProxy = Pair.Value;
			if (!PatchProxy.IsBypassed())
			{
				PatchProxy.Update();
			}
		}

		// Log stats
		SET_DWORD_STAT(STAT_AudioModulationBusCount, RefProxies.Buses.Num());
		SET_DWORD_STAT(STAT_AudioModulationMixCount, RefProxies.BusMixes.Num());
		SET_DWORD_STAT(STAT_AudioModulationGeneratorCount, RefProxies.Generators.Num());
		SET_DWORD_STAT(STAT_AudioModulationPatchCount, RefProxies.Patches.Num());
		SET_DWORD_STAT(STAT_AudioModulationProcQueueCount, CommandsProcessed);

#if !UE_BUILD_SHIPPING
 		Debugger.UpdateDebugData(InElapsed, RefProxies);
#endif // !UE_BUILD_SHIPPING
	}

	void FAudioModulationSystem::SaveMixToProfile(const USoundControlBusMix& InBusMix, const int32 InProfileIndex)
	{
		check(IsInGameThread());

		RunCommandOnProcessingThread([this, MixToSerialize = TWeakObjectPtr<const USoundControlBusMix>(&InBusMix), InProfileIndex]()
		{
			if (!MixToSerialize.IsValid())
			{
				return;
			}

			const FBusMixId MixId = static_cast<FBusMixId>(MixToSerialize->GetUniqueID());
			const FString   MixName = MixToSerialize->GetName();

			FBusMixHandle MixHandle = FBusMixHandle::Get(MixId, RefProxies.BusMixes);
			const bool bIsActive = MixHandle.IsValid();
			if (!MixHandle.IsValid())
			{
				UE_LOG(LogAudioModulation, Display, TEXT("Mix '%s' is inactive, saving default object to profile '%i'."), *MixName, InProfileIndex);
				AsyncTask(ENamedThreads::GameThread, [this, MixToSerialize, InProfileIndex]()
				{
					AudioModulation::FProfileSerializer::Serialize(*MixToSerialize.Get(), InProfileIndex);
				});
				return;
			}

			UE_LOG(LogAudioModulation, Display, TEXT("Mix '%s' is active, saving current mix proxy state to profile '%i'."), *MixName, InProfileIndex);
			AudioModulation::FModulatorBusMixProxy& MixProxy = MixHandle.FindProxy();
			TMap<FBusId, FSoundModulationMixValue> PassedStageInfo;
			for (TPair<FBusId, FModulatorBusMixStageProxy>& Pair : MixProxy.Stages)
			{
				FModulatorBusMixStageProxy& Stage = Pair.Value;
				PassedStageInfo.Add(Pair.Key, Stage.Value);
			}

			AsyncTask(ENamedThreads::GameThread, [this, PassedStageInfo, MixToSerialize, InProfileIndex]()
			{
				if (!MixToSerialize.IsValid())
				{
					return;
				}
						
				TMap<FBusId, FSoundModulationMixValue> StageInfo = PassedStageInfo;
				USoundControlBusMix* TempMix = NewObject<USoundControlBusMix>(GetTransientPackage(), *FGuid().ToString(EGuidFormats::Short));

				// Buses on proxy may differ than those on uobject definition, so iterate and find by cached ids
				// and add to temp mix to be serialized.
				for (TObjectIterator<USoundControlBus> Itr; Itr; ++Itr)
				{
					if (USoundControlBus* Bus = *Itr)
					{
						FBusId ItrBusId = static_cast<FBusId>(Bus->GetUniqueID());
						if (FSoundModulationMixValue* Value = StageInfo.Find(ItrBusId))
						{
							FSoundControlBusMixStage BusMixStage;
							BusMixStage.Bus = Bus;
							BusMixStage.Value = *Value;
							TempMix->MixStages.Add(MoveTemp(BusMixStage));
						}
					}
				}

				const FString MixPath = MixToSerialize->GetPathName();
				AudioModulation::FProfileSerializer::Serialize(*TempMix, InProfileIndex, &MixPath);
			});
		});
	}

	TArray<FSoundControlBusMixStage> FAudioModulationSystem::LoadMixFromProfile(const int32 InProfileIndex, USoundControlBusMix& OutBusMix)
	{
		const FString TempName = FGuid::NewGuid().ToString(EGuidFormats::Short);
		if (USoundControlBusMix* TempMix = NewObject<USoundControlBusMix>(GetTransientPackage(), *TempName))
		{
			const FString MixPath = OutBusMix.GetPathName();
			AudioModulation::FProfileSerializer::Deserialize(InProfileIndex, *TempMix, &MixPath);
			UpdateMix(TempMix->MixStages, OutBusMix);
			return TempMix->MixStages;
		}

		return TArray<FSoundControlBusMixStage>();
	}

	void FAudioModulationSystem::RunCommandOnProcessingThread(TUniqueFunction<void()> Cmd)
	{
		if (IsInProcessingThread())
		{
			Cmd();
		}
		else
		{
			ProcessingThreadCommandQueue.Enqueue(MoveTemp(Cmd));
		}
	}

	Audio::FModulatorTypeId FAudioModulationSystem::RegisterModulator(Audio::FModulatorHandleId InHandleId, const USoundModulatorBase* InModulatorBase, Audio::FModulationParameter& OutParameter)
	{
		OutParameter = GetParameter(OutParameter.ParameterName);

		if (!InModulatorBase)
		{
			return INDEX_NONE;
		}

		if (RegisterModulator<FPatchHandle, USoundModulationPatch, FModulationPatchSettings, FPatchProxyMap>(InHandleId, InModulatorBase, RefProxies.Patches, RefModulators.PatchMap))
		{
			return static_cast<Audio::FModulatorTypeId>(EModulatorType::Patch);
		}
			
		if (RegisterModulator<FBusHandle, USoundControlBus, FControlBusSettings, FBusProxyMap>(InHandleId, InModulatorBase, RefProxies.Buses, RefModulators.BusMap))
		{
			return static_cast<Audio::FModulatorTypeId>(EModulatorType::Bus);
		}

		if (RegisterModulator<FGeneratorHandle, USoundModulationGenerator, FModulationGeneratorSettings, FGeneratorProxyMap>(InHandleId, InModulatorBase, RefProxies.Generators, RefModulators.GeneratorMap))
		{
			return static_cast<Audio::FModulatorTypeId>(EModulatorType::Generator);
		}

		ensureMsgf(false, TEXT("Modulator type of '%s' failed to register: Unsupported type."), *InModulatorBase->GetName());
		return INDEX_NONE;
	}

	void FAudioModulationSystem::RegisterModulator(Audio::FModulatorHandleId InHandleId, Audio::FModulatorId InModulatorId)
	{
		RunCommandOnProcessingThread([this, InHandleId, InModulatorId]()
		{
			FPatchHandle PatchHandle = FPatchHandle::Get(static_cast<FPatchId>(InModulatorId), RefProxies.Patches);
			if (PatchHandle.IsValid())
			{
				if (TArray<uint32>* RefObjectIds = RefModulators.PatchMap.Find(PatchHandle))
				{
					RefObjectIds->Add(InHandleId);
				}
			}

			FBusHandle BusHandle = FBusHandle::Get(static_cast<FBusId>(InModulatorId), RefProxies.Buses);
			if (BusHandle.IsValid())
			{
				if (TArray<uint32>* RefObjectIds = RefModulators.BusMap.Find(BusHandle))
				{
					RefObjectIds->Add(InHandleId);
				}
			}

			FGeneratorHandle GeneratorHandle = FGeneratorHandle::Get(static_cast<FGeneratorId>(InModulatorId), RefProxies.Generators);
			if (GeneratorHandle.IsValid())
			{
				if (TArray<uint32>* RefObjectIds = RefModulators.GeneratorMap.Find(GeneratorHandle))
				{
					RefObjectIds->Add(InHandleId);
				}
			}
		});
	}

	void FAudioModulationSystem::SoloBusMix(const USoundControlBusMix& InBusMix)
	{
		RunCommandOnProcessingThread([this, BusMixSettings = FModulatorBusMixSettings(InBusMix, AudioDeviceId)]()
		{
			bool bMixActive = false;
			for (TPair<FBusMixId, FModulatorBusMixProxy>& Pair : RefProxies.BusMixes)
			{
				if (Pair.Key == BusMixSettings.GetId())
				{
					bMixActive = true;
				}
				else
				{
					Pair.Value.SetStopping();
				}
			}

			if (!bMixActive)
			{
				ActivateBusMix(BusMixSettings);
			}
		});
	}

	void FAudioModulationSystem::UnregisterModulator(const Audio::FModulatorHandle& InHandle)
	{
		RunCommandOnProcessingThread([this, ModId = InHandle.GetModulatorId(), HandleId = InHandle.GetHandleId()]()
		{
			FPatchHandle PatchHandle = FPatchHandle::Get(static_cast<FPatchId>(ModId), RefProxies.Patches);
			if (UnregisterModulator<FPatchHandle>(PatchHandle, RefModulators.PatchMap, HandleId))
			{
				return;
			}

			FBusHandle BusHandle = FBusHandle::Get(static_cast<FBusId>(ModId), RefProxies.Buses);
			if (UnregisterModulator<FBusHandle>(BusHandle, RefModulators.BusMap, HandleId))
			{
				return;
			}

			FGeneratorHandle GeneratorHandle = FGeneratorHandle::Get(static_cast<FGeneratorId>(ModId), RefProxies.Generators);
			if (UnregisterModulator<FGeneratorHandle>(GeneratorHandle, RefModulators.GeneratorMap, HandleId))
			{
				return;
			}
		});
	}

	void FAudioModulationSystem::UpdateMix(const TArray<FSoundControlBusMixStage>& InStages, USoundControlBusMix& InOutMix, bool bInUpdateObject, float InFadeTime)
	{
		if (bInUpdateObject)
		{
			TMap<uint32, const FSoundControlBusMixStage*> UpdatedStageBusses;
			for (const FSoundControlBusMixStage& Stage : InStages)
			{
				if (Stage.Bus)
				{
					UpdatedStageBusses.Add(Stage.Bus->GetUniqueID(), &Stage);
				}
			}

			bool bMarkDirty = false;
			for (FSoundControlBusMixStage& Stage : InOutMix.MixStages)
			{
				if (!Stage.Bus)
				{
					continue;
				}

				if (const FSoundControlBusMixStage* BusStage = UpdatedStageBusses.FindRef(Stage.Bus->GetUniqueID()))
				{
					Stage = *BusStage;
					bMarkDirty = true;
				}
			}
			InOutMix.MarkPackageDirty();
		}

		const FBusMixId MixId = static_cast<FBusMixId>(InOutMix.GetUniqueID());

		TArray<FModulatorBusMixStageSettings> StageSettings;
		for (const FSoundControlBusMixStage& Stage : InStages)
		{
			if (Stage.Bus)
			{
				StageSettings.Emplace(Stage, AudioDeviceId);
			}
		}
	
		RunCommandOnProcessingThread([this, MixId, StageSettings, InFadeTime]()
		{
			if (FModulatorBusMixProxy* BusMixes = RefProxies.BusMixes.Find(MixId))
			{
				BusMixes->SetMix(StageSettings, InFadeTime);
			}
		});
	}

	void FAudioModulationSystem::UpdateMixByFilter(
		const FString& InAddressFilter,
		const TSubclassOf<USoundModulationParameter>& InParamClassFilter,
		USoundModulationParameter* InParamFilter,
		float InValue,
		float InFadeTime,
		USoundControlBusMix& InOutMix,
		bool bInUpdateObject)
	{
		const uint32 ParamClassId = InParamClassFilter ? InParamClassFilter->GetUniqueID() : INDEX_NONE;
		const uint32 ParamId = InParamFilter ? InParamFilter->GetUniqueID() : INDEX_NONE;

		if (bInUpdateObject)
		{
			bool bMarkDirty = false;
			for (FSoundControlBusMixStage& Stage : InOutMix.MixStages)
			{
				if (!Stage.Bus)
				{
					continue;
				}

				if (USoundModulationParameter* Parameter = Stage.Bus->Parameter)
				{
					if (ParamId != INDEX_NONE && ParamId != Parameter->GetUniqueID())
					{
						continue;
					}

					if (UClass* Class = Parameter->GetClass())
					{
						if (ParamClassId != INDEX_NONE && ParamClassId != Class->GetUniqueID())
						{
							continue;
						}
					}
				}

				if (!FAudioAddressPattern::PartsMatch(InAddressFilter, Stage.Bus->Address))
				{
					continue;
				}

				Stage.Value.TargetValue = InValue;
				Stage.Value.SetActiveFade(FSoundModulationMixValue::EActiveFade::Override, InFadeTime);
				bMarkDirty = true;
			}

			if (bMarkDirty)
			{
				InOutMix.MarkPackageDirty();
			}
		}

		const FString	AddressFilter = InAddressFilter;
		const FBusMixId MixId = static_cast<FBusMixId>(InOutMix.GetUniqueID());
		RunCommandOnProcessingThread([this, ParamClassId, ParamId, MixId, AddressFilter, InValue, InFadeTime]()
		{
			if (FModulatorBusMixProxy* MixProxy = RefProxies.BusMixes.Find(MixId))
			{
				MixProxy->SetMixByFilter(AddressFilter, ParamClassId, ParamId, InValue, InFadeTime);
			}
		});
	}

	void FAudioModulationSystem::UpdateMix(const USoundControlBusMix& InMix, float InFadeTime)
	{
		RunCommandOnProcessingThread([this, MixSettings = FModulatorBusMixSettings(InMix, AudioDeviceId), InFadeTime]()
		{
			FBusMixHandle BusMixHandle = FBusMixHandle::Get(MixSettings.GetId(), RefProxies.BusMixes);
			if (BusMixHandle.IsValid())
			{
				FModulatorBusMixProxy& MixProxy = BusMixHandle.FindProxy();
				if (MixProxy.GetStatus() == FModulatorBusMixProxy::EStatus::Enabled)
				{
					MixProxy = MixSettings;
					for (TPair<FBusId, FModulatorBusMixStageProxy>& Stage : MixProxy.Stages)
					{
						Stage.Value.Value.SetActiveFade(FSoundModulationMixValue::EActiveFade::Override, InFadeTime);
					}
				}
			}
#if !UE_BUILD_SHIPPING
			else
			{
				UE_LOG(LogAudioModulation, Verbose, TEXT("Update to '%s' Ignored: Control Bus Mix is inactive."), *MixSettings.GetName());
			}
#endif // !UE_BUILD_SHIPPING
		});
	}

	void FAudioModulationSystem::UpdateModulator(const USoundModulatorBase& InModulator)
	{
		if (const USoundModulationGenerator* InGenerator = Cast<USoundModulationGenerator>(&InModulator))
		{
			RunCommandOnProcessingThread([this, GeneratorSettings = FModulationGeneratorSettings(*InGenerator, AudioDeviceId)]()
			{
				FGeneratorHandle GeneratorHandle = FGeneratorHandle::Get(GeneratorSettings.GetId(), RefProxies.Generators);
				if (GeneratorHandle.IsValid())
				{
					GeneratorHandle.FindProxy() = GeneratorSettings;
				}
#if !UE_BUILD_SHIPPING
				else
				{
					UE_LOG(LogAudioModulation, Verbose, TEXT("Update to '%s' Ignored: Generator is inactive."), *GeneratorSettings.GetName());
				}
#endif // !UE_BUILD_SHIPPING
			});
		}

		if (const USoundControlBus* InBus = Cast<USoundControlBus>(&InModulator))
		{
			RunCommandOnProcessingThread([this, BusSettings = FControlBusSettings(*InBus, AudioDeviceId)]()
			{
				FBusHandle BusHandle = FBusHandle::Get(BusSettings.GetId(), RefProxies.Buses);
				if (BusHandle.IsValid())
				{
					BusHandle.FindProxy() = BusSettings;
				}
#if !UE_BUILD_SHIPPING
				else
				{
					UE_LOG(LogAudioModulation, Verbose, TEXT("Update to '%s' Ignored: Control Bus is inactive."), *BusSettings.GetName());
				}
#endif // !UE_BUILD_SHIPPING
			});
		}

		if (const USoundControlBusMix* InMix = Cast<USoundControlBusMix>(&InModulator))
		{
			RunCommandOnProcessingThread([this, BusMixSettings = FModulatorBusMixSettings(*InMix, AudioDeviceId)]()
			{
				FBusMixHandle BusMixHandle = FBusMixHandle::Get(BusMixSettings.GetId(), RefProxies.BusMixes);
				if (BusMixHandle.IsValid())
				{
					BusMixHandle.FindProxy() = BusMixSettings;
				}
#if !UE_BUILD_SHIPPING
				else
				{
					UE_LOG(LogAudioModulation, Verbose, TEXT("Update to '%s' Ignored: Control Bus Mix is inactive."), *BusMixSettings.GetName());
				}
#endif // !UE_BUILD_SHIPPING
			});
		}

		if (const USoundModulationPatch* InPatch = Cast<USoundModulationPatch>(&InModulator))
		{
			RunCommandOnProcessingThread([this, PatchSettings = FModulationPatchSettings(*InPatch, AudioDeviceId)]()
			{
				FPatchHandle PatchHandle = FPatchHandle::Get(PatchSettings.GetId(), RefProxies.Patches);
				if (PatchHandle.IsValid())
				{
					FModulationPatchRefProxy& PatchProxy = PatchHandle.FindProxy();
					PatchProxy = PatchSettings;
				}
#if !UE_BUILD_SHIPPING
				else
				{
					UE_LOG(LogAudioModulation, Verbose, TEXT("Update to '%s' Ignored: Patch is inactive."), *PatchSettings.GetName());
				}
#endif // !UE_BUILD_SHIPPING
			});
		}
	}
} // namespace AudioModulation
#endif // WITH_AUDIOMODULATION
