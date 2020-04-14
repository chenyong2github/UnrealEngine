// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioModulationSystem.h"

#if WITH_AUDIOMODULATION
#include "Audio/AudioAddressPattern.h"
#include "AudioModulationLogging.h"
#include "AudioModulationProfileSerializer.h"
#include "AudioThread.h"
#include "Engine/Engine.h"
#include "IAudioModulation.h"
#include "Misc/CoreDelegates.h"
#include "SoundControlBusProxy.h"
#include "SoundControlBusMixProxy.h"
#include "SoundModulatorLFOProxy.h"
#include "SoundModulationPatchProxy.h"
#include "SoundModulationProxy.h"
#include "UObject/UObjectIterator.h"
#include "UObject/WeakObjectPtr.h"


#if !UE_BUILD_SHIPPING
#include "AudioModulationDebugger.h"
#include "Async/Async.h"
#endif // !UE_BUILD_SHIPPING

DECLARE_DWORD_COUNTER_STAT(TEXT("Bus Count"),	STAT_AudioModulationBusCount, STATGROUP_AudioModulation)
DECLARE_DWORD_COUNTER_STAT(TEXT("LFO Count"),	STAT_AudioModulationLFOCount, STATGROUP_AudioModulation)
DECLARE_DWORD_COUNTER_STAT(TEXT("Mix Count"),	STAT_AudioModulationMixCount, STATGROUP_AudioModulation)
DECLARE_DWORD_COUNTER_STAT(TEXT("Patch Count"), STAT_AudioModulationPatchCount, STATGROUP_AudioModulation)


namespace AudioModulation
{
	struct FProfileChannelInfo
	{
		USoundControlBusBase* Bus;
		FSoundModulationValue Value;

		FProfileChannelInfo(const FModulatorBusMixChannelProxy& InProxy)
			: Bus(nullptr)
			, Value(InProxy.Value)
		{
		}
	};

	FAudioModulationSystem::FAudioModulationSystem()
	{
	}

	void FAudioModulationSystem::Initialize(const FAudioPluginInitializationParams& InitializationParams)
	{
		SourceSettings.AddDefaulted(InitializationParams.NumSources);
	}

#if WITH_EDITOR
	void FAudioModulationSystem::OnEditPluginSettings(const USoundModulationPluginSourceSettingsBase& InSettings)
	{
		const TWeakObjectPtr<const USoundModulationPluginSourceSettingsBase> SettingsPtr = &InSettings;
		RunCommandOnAudioThread([this, SettingsPtr]()
		{
			if (!SettingsPtr.IsValid())
			{
				return;
			}

			const USoundModulationSettings* Settings = CastChecked<USoundModulationSettings>(SettingsPtr.Get());
			const uint32 SettingsId = Settings->GetUniqueID();
			for (FModulationSettingsProxy& SourceSetting : SourceSettings)
			{
				if (SourceSetting.GetId() == SettingsId)
				{
					SourceSetting = FModulationSettingsProxy(*Settings, *this);
				}
			}

			for (TPair<uint32, FModulationSettingsProxy>& Pair : SoundSettings)
			{
				if (Pair.Value.GetId() == SettingsId)
				{
					Pair.Value = FModulationSettingsProxy(*Settings, *this);
				}
			}
		});
	}
#endif // WITH_EDITOR

	void FAudioModulationSystem::OnInitSound(ISoundModulatable& InSound, const USoundModulationPluginSourceSettingsBase& InSettings)
	{
		check(IsInAudioThread());

		const uint32 SoundId = InSound.GetObjectId();
		if (!SoundSettings.Contains(SoundId))
		{
			const USoundModulationSettings* Settings = CastChecked<USoundModulationSettings>(&InSettings);
			SoundSettings.Add(SoundId, FModulationSettingsProxy(*Settings, *this));
		}
	}

	void FAudioModulationSystem::OnInitSource(const uint32 InSourceId, const FName& AudioComponentUserId, const uint32 NumChannels, const USoundModulationPluginSourceSettingsBase& InSettings)
	{
		check(IsInAudioThread());

		if (const USoundModulationSettings* Settings = CastChecked<USoundModulationSettings>(&InSettings))
		{
			SourceSettings[InSourceId] = FModulationSettingsProxy(*Settings, *this);
		}
	}

	void FAudioModulationSystem::OnReleaseSource(const uint32 InSourceId)
	{
		check(IsInAudioThread());

		SourceSettings[InSourceId] = FModulationSettingsProxy();
	}

	void FAudioModulationSystem::OnReleaseSound(ISoundModulatable& InSound)
	{
		check(IsInAudioThread());
		check(InSound.GetObjectId() != INDEX_NONE);

		if (InSound.GetPlayCount() == 0)
		{
			SoundSettings.Remove(InSound.GetObjectId());
		}
	}

#if !UE_BUILD_SHIPPING
	bool FAudioModulationSystem::OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream)
	{
		return ViewportClient ? Debugger.OnPostHelp(*ViewportClient, Stream) : true;
	}

	int32 FAudioModulationSystem::OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation)
	{
		return Canvas ? Debugger.OnRenderStat(*Canvas, X, Y, Font) : Y;
	}

	bool FAudioModulationSystem::OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream)
	{
		return ViewportClient ? Debugger.OnToggleStat(*ViewportClient, Stream) : true;
	}
#endif // !UE_BUILD_SHIPPING

	void FAudioModulationSystem::ActivateBus(const USoundControlBusBase& InBus)
	{
		const TWeakObjectPtr<const USoundControlBusBase>BusPtr = &InBus;
		RunCommandOnAudioThread([this, BusPtr]()
		{
			if (BusPtr.IsValid())
			{
				auto OnCreate = [this, BusPtr](FControlBusProxy& NewProxy)
				{
					NewProxy.InitLFOs(*BusPtr.Get());
				};

				FBusHandle BusHandle = FBusHandle::Create(*BusPtr.Get(), RefProxies.Buses, *this, OnCreate);
				ManuallyActivatedBuses.Add(MoveTemp(BusHandle));
			}
		});

	}

	void FAudioModulationSystem::ActivateBusMix(const USoundControlBusMix& InBusMix)
	{
		const TWeakObjectPtr<const USoundControlBusMix>BusMixPtr = &InBusMix;
		RunCommandOnAudioThread([this, BusMixPtr]()
		{
			if (BusMixPtr.IsValid())
			{
				FBusMixHandle BusMixHandle = FBusMixHandle::Get(*BusMixPtr.Get(), RefProxies.BusMixes);
				if (BusMixHandle.IsValid())
				{
					BusMixHandle.FindProxy().SetEnabled(*BusMixPtr.Get());
				}
				else
				{
					BusMixHandle = FBusMixHandle::Create(*BusMixPtr.Get(), RefProxies.BusMixes, *this);
				}

				ManuallyActivatedBusMixes.Add(MoveTemp(BusMixHandle));
			}
		});

	}

	void FAudioModulationSystem::ActivateLFO(const USoundBusModulatorLFO& InLFO)
	{
		const TWeakObjectPtr<const USoundBusModulatorLFO>LFOPtr = &InLFO;
		RunCommandOnAudioThread([this, LFOPtr]()
		{
			if (LFOPtr.IsValid())
			{
				FLFOHandle LFOHandle = FLFOHandle::Create(*LFOPtr.Get(), RefProxies.LFOs, *this);
				ManuallyActivatedLFOs.Add(MoveTemp(LFOHandle));
			}
		});
	}

	bool FAudioModulationSystem::CalculateModulationValue(FModulationPatchProxy& OutProxy, float& OutValue) const
	{
		if (OutProxy.IsBypassed())
		{
			return false;
		}

		const float InitValue = OutValue;
		OutValue = OutProxy.Update();

		return !FMath::IsNearlyEqual(InitValue, OutValue);
	}

	float FAudioModulationSystem::CalculateInitialVolume(const USoundModulationPluginSourceSettingsBase& SettingsBase)
	{
		check(IsInAudioThread());

		const USoundModulationSettings* Settings = CastChecked<USoundModulationSettings>(&SettingsBase);
		FModulationPatchProxy VolumePatch(Settings->Volume, *this);

		return VolumePatch.Update();
	}

	void FAudioModulationSystem::DeactivateBus(const USoundControlBusBase& InBus)
	{
		const TWeakObjectPtr<const USoundControlBusBase>BusPtr = &InBus;
		RunCommandOnAudioThread([this, BusPtr]()
		{
			if (BusPtr.IsValid())
			{
				const USoundControlBusBase& Bus = *BusPtr.Get();
				FBusHandle BusHandle = FBusHandle::Create(Bus, RefProxies.Buses, *this);
				ManuallyActivatedBuses.Remove(MoveTemp(BusHandle));
			}
		});
	}

	void FAudioModulationSystem::DeactivateBusMix(const USoundControlBusMix& InBusMix)
	{
		const TWeakObjectPtr<const USoundControlBusMix> BusMixPtr = &InBusMix;
		RunCommandOnAudioThread([this, BusMixPtr]()
		{
			if (BusMixPtr.IsValid())
			{
				const USoundControlBusMix& BusMix = *BusMixPtr.Get();
				FBusMixHandle BusMixHandle = FBusMixHandle::Get(BusMix, RefProxies.BusMixes);
				if (BusMixHandle.IsValid())
				{
					FModulatorBusMixProxy& MixProxy = BusMixHandle.FindProxy();
					MixProxy.SetStopping();
				}
			}
		});
	}

	void FAudioModulationSystem::DeactivateLFO(const USoundBusModulatorLFO& InLFO)
	{
		const TWeakObjectPtr<const USoundBusModulatorLFO>LFOPtr = &InLFO;
		RunCommandOnAudioThread([this, LFOPtr]()
		{
			if (LFOPtr.IsValid())
			{
				const USoundBusModulatorLFO& LFO = *LFOPtr.Get();
				FLFOHandle LFOHandle = FLFOHandle::Create(LFO, RefProxies.LFOs, *this);
				ManuallyActivatedLFOs.Remove(MoveTemp(LFOHandle));
			}
		});
	}

	bool FAudioModulationSystem::IsBusActive(const FBusId InBusId) const
	{
		check(IsInAudioThread());

		return RefProxies.Buses.Contains(InBusId);
	}

	bool FAudioModulationSystem::ProcessControls(const uint32 InSourceId, FSoundModulationControls& OutControls)
	{
		check(IsInAudioThread());

		bool bControlsUpdated = false;

		FModulationSettingsProxy& Settings = SourceSettings[InSourceId];

		if (Settings.Volume.IsBypassed())
		{
			OutControls.Volume = 1.0f;
		}
		else
		{
			bControlsUpdated |= CalculateModulationValue(Settings.Volume, OutControls.Volume);
		}

		if (Settings.Pitch.IsBypassed())
		{
			OutControls.Pitch = 1.0f;
		}
		else
		{
			bControlsUpdated |= CalculateModulationValue(Settings.Pitch, OutControls.Pitch);
		}

		if (Settings.Highpass.IsBypassed())
		{
			OutControls.Highpass = MIN_FILTER_FREQUENCY;
		}
		else
		{
			bControlsUpdated |= CalculateModulationValue(Settings.Highpass, OutControls.Highpass);
		}

		if (Settings.Lowpass.IsBypassed())
		{
			OutControls.Lowpass = MAX_FILTER_FREQUENCY;
		}
		else
		{
			bControlsUpdated |= CalculateModulationValue(Settings.Lowpass, OutControls.Lowpass);
		}

		return bControlsUpdated;
	}

	void FAudioModulationSystem::ProcessModulators(float Elapsed)
	{
		check(IsInAudioThread());

		// Pump command queue
		TUniqueFunction<void()> Command;
		while (AudioThreadCommandQueue.Dequeue(Command))
		{
			Command();
		}

		// Update LFOs (prior to bus mixing to avoid single-frame latency)
		for (TPair<FLFOId, FModulatorLFOProxy>& Pair : RefProxies.LFOs)
		{
			Pair.Value.Update(Elapsed);
		}

		// Reset buses & refresh cached LFO
		for (TPair<FBusId, FControlBusProxy>& Pair : RefProxies.Buses)
		{
			Pair.Value.Reset();
			Pair.Value.MixLFO();
		}

		// Update mix values and apply to prescribed buses.
		// Track bus mixes ready to remove
		TSet<FBusMixId> StoppedMixIds;
		for (TPair<FBusMixId, FModulatorBusMixProxy>& Pair : RefProxies.BusMixes)
		{
			const FModulatorBusMixProxy::EStatus LastStatus = Pair.Value.GetStatus();
			Pair.Value.Update(Elapsed, RefProxies.Buses);
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

		// Send updated patch/bus values to render thread
		Audio::FControlModulatorValueMap UpdatedModValueMap;
		for (TPair<FPatchId, FModulationPatchRefProxy>& Pair : RefProxies.Patches)
		{
			FModulationPatchRefProxy& PatchProxy = Pair.Value;
			if (!PatchProxy.IsBypassed())
			{
				const float NewValue = PatchProxy.Update();
				UpdatedModValueMap.Add(Pair.Key, NewValue);
			}
		}

		for (TPair<FBusId, FControlBusProxy>& Pair : RefProxies.Buses)
		{
			FControlBusProxy& BusProxy = Pair.Value;
			{
				if (!BusProxy.IsBypassed())
				{
					UpdatedModValueMap.Add(Pair.Key, BusProxy.GetValue());
				}
			}
		}

		for (TPair<FLFOId, FModulatorLFOProxy>& Pair : RefProxies.LFOs)
		{
			FModulatorLFOProxy& LFOProxy = Pair.Value;
			{
				if (!LFOProxy.IsBypassed())
				{
					UpdatedModValueMap.Add(Pair.Key, LFOProxy.GetValue());
				}
			}
		}

		RunCommandOnAudioRenderThread([this, UpdatedModValueMap]()
		{
			ModValues_RenderThread = UpdatedModValueMap;
		});

		// Log stats
		SET_DWORD_STAT(STAT_AudioModulationBusCount, RefProxies.Buses.Num());
		SET_DWORD_STAT(STAT_AudioModulationMixCount, RefProxies.BusMixes.Num());
		SET_DWORD_STAT(STAT_AudioModulationLFOCount, RefProxies.LFOs.Num());
		SET_DWORD_STAT(STAT_AudioModulationPatchCount, RefProxies.Patches.Num());

#if !UE_BUILD_SHIPPING
		Debugger.UpdateDebugData(RefProxies);
#endif // !UE_BUILD_SHIPPING
	}

	void FAudioModulationSystem::SaveMixToProfile(const USoundControlBusMix& InBusMix, const int32 InProfileIndex)
	{
		check(IsInGameThread());

		const TWeakObjectPtr<const USoundControlBusMix> MixToSerialize = &InBusMix;
		RunCommandOnAudioThread([this, MixToSerialize, InProfileIndex]()
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
			TMap<FBusId, FSoundModulationValue> PassedChannelInfo;
			for (TPair<FBusId, FModulatorBusMixChannelProxy>& Pair : MixProxy.Channels)
			{
				FModulatorBusMixChannelProxy& Channel = Pair.Value;
				PassedChannelInfo.Add(Pair.Key, Channel.Value);
			}

			AsyncTask(ENamedThreads::GameThread, [this, PassedChannelInfo, MixToSerialize, InProfileIndex]()
			{
				if (!MixToSerialize.IsValid())
				{
					return;
				}
						
				TMap<FBusId, FSoundModulationValue> ChannelInfo = PassedChannelInfo;
				USoundControlBusMix* TempMix = NewObject<USoundControlBusMix>(GetTransientPackage(), *FGuid().ToString(EGuidFormats::Short));

				// Buses on proxy may differ than those on uobject definition, so iterate and find by cached ids
				// and add to temp mix to be serialized.
				for (TObjectIterator<USoundControlBusBase> Itr; Itr; ++Itr)
				{
					if (USoundControlBusBase* Bus = *Itr)
					{
						FBusId ItrBusId = static_cast<FBusId>(Bus->GetUniqueID());
						if (FSoundModulationValue* Value = ChannelInfo.Find(ItrBusId))
						{
							FSoundControlBusMixChannel BusMixChannel;
							BusMixChannel.Bus = Bus;
							BusMixChannel.Value = *Value;
							TempMix->Channels.Add(MoveTemp(BusMixChannel));
						}
					}
				}

				const FString   MixPath = MixToSerialize->GetPathName();
				AudioModulation::FProfileSerializer::Serialize(*TempMix, InProfileIndex, &MixPath);
			});
		});
	}

	TArray<FSoundControlBusMixChannel> FAudioModulationSystem::LoadMixFromProfile(const int32 InProfileIndex, USoundControlBusMix& OutBusMix)
	{
		const FString TempName = FGuid::NewGuid().ToString(EGuidFormats::Short);
		if (USoundControlBusMix* TempMix = NewObject<USoundControlBusMix>(GetTransientPackage(), *TempName))
		{
			const FString MixPath = OutBusMix.GetPathName();
			AudioModulation::FProfileSerializer::Deserialize(InProfileIndex, *TempMix, &MixPath);
			UpdateMix(TempMix->Channels, OutBusMix);
			return TempMix->Channels;
		}

		return TArray<FSoundControlBusMixChannel>();
	}

	void FAudioModulationSystem::RunCommandOnAudioThread(TUniqueFunction<void()> Cmd)
	{
		if (IsInAudioThread())
		{
			Cmd();
		}
		else
		{
			AudioThreadCommandQueue.Enqueue(MoveTemp(Cmd));
		}
	}

	void FAudioModulationSystem::RunCommandOnAudioRenderThread(TUniqueFunction<void()> Cmd)
	{
		RenderThreadCommandQueue.Enqueue(MoveTemp(Cmd));
	}

	template <typename THandleType, typename TModType, typename TMapType>
	bool FAudioModulationSystem::RegisterModulator(FAudioModulationSystem* InSystem, uint32 InParentId, const USoundModulatorBase& InModulatorBase, TMapType& ProxyMap, TMap<THandleType, TArray<uint32>>& ModMap)
	{
		if (const TModType* Mod = Cast<TModType>(&InModulatorBase))
		{
			const TWeakObjectPtr<const TModType> ModPtr = Mod;
			TMapType* PassedProxyMap = &ProxyMap;
			TMap<THandleType, TArray<uint32>>* PassedModMap = &ModMap;
			RunCommandOnAudioThread([InSystem, ModPtr, InParentId, PassedProxyMap, PassedModMap]()
			{
				check(PassedProxyMap);
				check(PassedModMap);
					
				if (ModPtr.IsValid())
				{
					THandleType Handle = THandleType::Create(*ModPtr.Get(), *PassedProxyMap, *InSystem);
					PassedModMap->FindOrAdd(Handle).Add(InParentId);
				}
			});
			return true;
		}

		return false;
	}

	bool FAudioModulationSystem::RegisterModulator(uint32 InParentId, const USoundModulatorBase& InModulatorBase)
	{
		if (RegisterModulator<FPatchHandle, USoundModulationPatch, FPatchProxyMap>(this, InParentId, InModulatorBase, RefProxies.Patches, RefModulators.PatchMap))
		{
			return true;
		}
			
		if (RegisterModulator<FBusHandle, USoundControlBusBase, FBusProxyMap>(this, InParentId, InModulatorBase, RefProxies.Buses, RefModulators.BusMap))
		{
			return true;
		}

		if (RegisterModulator<FLFOHandle, USoundBusModulatorLFO, FLFOProxyMap>(this, InParentId, InModulatorBase, RefProxies.LFOs, RefModulators.LFOMap))
		{
			return true;
		}

		UE_LOG(LogAudioModulation, Warning, TEXT("Modulator type  of '%s' unsupported by generic control modulation."), *InModulatorBase.GetName());
		return false;
	}

	bool FAudioModulationSystem::RegisterModulator(uint32 InParentId, Audio::FModulatorId InModulatorId)
	{
		RunCommandOnAudioThread([this, InParentId, InModulatorId]()
		{
			FPatchHandle PatchHandle = FPatchHandle::Get(static_cast<FPatchId>(InModulatorId), RefProxies.Patches);
			if (PatchHandle.IsValid())
			{
				if (TArray<uint32>* RefObjectIds = RefModulators.PatchMap.Find(PatchHandle))
				{
					RefObjectIds->Add(InParentId);
				}
			}

			FBusHandle BusHandle = FBusHandle::Get(static_cast<FBusId>(InModulatorId), RefProxies.Buses);
			if (BusHandle.IsValid())
			{
				if (TArray<uint32>* RefObjectIds = RefModulators.BusMap.Find(BusHandle))
				{
					RefObjectIds->Add(InParentId);
				}
			}

			FLFOHandle LFOHandle = FLFOHandle::Get(static_cast<FLFOId>(InModulatorId), RefProxies.LFOs);
			if (LFOHandle.IsValid())
			{
				if (TArray<uint32>* RefObjectIds = RefModulators.LFOMap.Find(LFOHandle))
				{
					RefObjectIds->Add(InParentId);
				}
			}
		});

		return true;
	}

	bool FAudioModulationSystem::GetModulatorValue(const Audio::FModulatorHandle& InModulatorHandle, float& OutValue)
	{
		if (float* NewValue = ModValues_RenderThread.Find(InModulatorHandle.GetId()))
		{
			OutValue = *NewValue;
			return true;
		}

		return false;
	}

	void FAudioModulationSystem::OnBeginAudioRenderThreadUpdate()
	{
		// Execute lambda functions passed from audio thread
		// to update publicly available modulation values.
		TUniqueFunction<void()> Command;
		while (RenderThreadCommandQueue.Dequeue(Command))
		{
			Command();
		}
	}

	template <typename THandleType>
	bool FAudioModulationSystem::UnregisterModulator(THandleType PatchHandle, TMap<THandleType, TArray<uint32>>& HandleMap, const uint32 ParentId)
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

	void FAudioModulationSystem::UnregisterModulator(const Audio::FModulatorHandle& InHandle)
	{
		const Audio::FModulatorId ModId = InHandle.GetId();
		RunCommandOnAudioRenderThread([this, ModId]()
		{
			ModValues_RenderThread.Remove(ModId);
		});

		RunCommandOnAudioThread([this, ModId, ParentId = InHandle.GetParentId()]()
		{
			FPatchHandle PatchHandle = FPatchHandle::Get(static_cast<FPatchId>(ModId), RefProxies.Patches);
			if (UnregisterModulator<FPatchHandle>(PatchHandle, RefModulators.PatchMap, ParentId))
			{
				return;
			}

			FBusHandle BusHandle = FBusHandle::Get(static_cast<FBusId>(ModId), RefProxies.Buses);
			if (UnregisterModulator<FBusHandle>(BusHandle, RefModulators.BusMap, ParentId))
			{
				return;
			}

			FLFOHandle LFOHandle = FLFOHandle::Get(static_cast<FLFOId>(ModId), RefProxies.LFOs);
			if (UnregisterModulator<FLFOHandle>(LFOHandle, RefModulators.LFOMap, ParentId))
			{
				return;
			}
		});
	}

	void FAudioModulationSystem::UpdateMix(const TArray<FSoundControlBusMixChannel>& InChannels, USoundControlBusMix& InOutMix, bool bInUpdateObject)
	{
		if (bInUpdateObject)
		{
			TMap<uint32, const FSoundControlBusMixChannel*> UpdatedChannelBusses;
			for (const FSoundControlBusMixChannel& Channel : InChannels)
			{
				if (Channel.Bus)
				{
					UpdatedChannelBusses.Add(Channel.Bus->GetUniqueID(), &Channel);
				}
			}

			bool bMarkDirty = false;
			for (FSoundControlBusMixChannel& Channel : InOutMix.Channels)
			{
				if (!Channel.Bus)
				{
					continue;
				}

				if (const FSoundControlBusMixChannel* BusChannel = UpdatedChannelBusses.FindRef(Channel.Bus->GetUniqueID()))
				{
					Channel = *BusChannel;
					bMarkDirty = true;
				}
			}
			InOutMix.MarkPackageDirty();
		}

		const FBusMixId MixId = static_cast<FBusMixId>(InOutMix.GetUniqueID());
		RunCommandOnAudioThread([this, MixId, InChannels]()
		{
			if (FModulatorBusMixProxy* BusMixes = RefProxies.BusMixes.Find(MixId))
			{
				BusMixes->SetMix(InChannels);
			}
		});
	}

	void FAudioModulationSystem::UpdateMixByFilter(
		const FString&							 InAddressFilter,
		const TSubclassOf<USoundControlBusBase>& InBusClass,
		const FSoundModulationValue&			 InValue,
		USoundControlBusMix&					 InOutMix,
		bool									 bInUpdateObject)
	{
		const uint32 ClassId = InBusClass ? InBusClass->GetUniqueID() : USoundControlBusBase::StaticClass()->GetUniqueID();

		if (bInUpdateObject)
		{
			bool bMarkDirty = false;
			static const uint32 BaseBusClassId = USoundControlBusBase::StaticClass()->GetUniqueID();
			for (FSoundControlBusMixChannel& Channel : InOutMix.Channels)
			{
				if (!Channel.Bus)
				{
					continue;
				}

				if (ClassId != BaseBusClassId && Channel.Bus->GetClass()->GetUniqueID() != ClassId)
				{
					continue;
				}

				if (!FAudioAddressPattern::PartsMatch(InAddressFilter, Channel.Bus->Address))
				{
					continue;
				}

				Channel.Value.TargetValue = InValue.TargetValue;

				if (InValue.AttackTime >= 0.0f)
				{
					Channel.Value.AttackTime = InValue.AttackTime;
				}

				if (InValue.ReleaseTime >= 0.0f)
				{
					Channel.Value.ReleaseTime = InValue.ReleaseTime;
				}
				bMarkDirty = true;
			}

			if (bMarkDirty)
			{
				InOutMix.MarkPackageDirty();
			}
		}

		const FString	AddressFilter = InAddressFilter;
		const FBusMixId MixId = static_cast<FBusMixId>(InOutMix.GetUniqueID());
		RunCommandOnAudioThread([this, ClassId, MixId, AddressFilter, InValue]()
		{
			if (FModulatorBusMixProxy* MixProxy = RefProxies.BusMixes.Find(MixId))
			{
				MixProxy->SetMixByFilter(AddressFilter, ClassId, InValue);
			}
		});
	}

	void FAudioModulationSystem::UpdateMix(const USoundControlBusMix& InMix)
	{
		const TWeakObjectPtr<const USoundControlBusMix> BusMixPtr = &InMix;
		RunCommandOnAudioThread([this, BusMixPtr]()
		{
			if (BusMixPtr.IsValid())
			{
				const USoundControlBusMix& Mix = *BusMixPtr.Get();
				FBusMixHandle BusMixHandle = FBusMixHandle::Get(Mix, RefProxies.BusMixes);
				if (BusMixHandle.IsValid())
				{
					BusMixHandle.FindProxy() = Mix;
				}
				else
				{
					UE_LOG(LogAudioModulation, Verbose, TEXT("Update to '%s' Ignored: Control Bus Mix is inactive."), *Mix.GetName());
				}
			}
		});
	}

	void FAudioModulationSystem::UpdateModulator(const USoundModulatorBase& InModulator)
	{
		if (const USoundBusModulatorLFO* InLFO = Cast<USoundBusModulatorLFO>(&InModulator))
		{
			const TWeakObjectPtr<const USoundBusModulatorLFO> LFOPtr = InLFO;
			RunCommandOnAudioThread([this, LFOPtr]()
			{
				if (LFOPtr.IsValid())
				{
					const USoundBusModulatorLFO& LFO = *LFOPtr.Get();
					FLFOHandle LFOHandle = FLFOHandle::Get(LFO, RefProxies.LFOs);
					if (LFOHandle.IsValid())
					{
						LFOHandle.FindProxy() = LFO;
					}
					else
					{
						UE_LOG(LogAudioModulation, Verbose, TEXT("Update to '%s' Ignored: LFO is inactive."), *LFO.GetName());
					}
				}
			});
		}

		if (const USoundControlBusBase* InBus = Cast<USoundControlBusBase>(&InModulator))
		{
			const TWeakObjectPtr<const USoundControlBusBase> BusPtr = InBus;
			RunCommandOnAudioThread([this, BusPtr]()
			{
				if (BusPtr.IsValid())
				{
					const USoundControlBusBase* Bus = BusPtr.Get();
					FBusHandle BusHandle = FBusHandle::Get(*Bus, RefProxies.Buses);
					if (BusHandle.IsValid())
					{
						FControlBusProxy& Proxy = BusHandle.FindProxy();
						Proxy = *Bus;
						Proxy.InitLFOs(*Bus);
					}
					else
					{
						UE_LOG(LogAudioModulation, Verbose, TEXT("Update to '%s' Ignored: Control Bus is inactive."), *Bus->GetName());
					}
				}
			});
		}

		if (const USoundControlBusMix* InMix = Cast<USoundControlBusMix>(&InModulator))
		{
			const TWeakObjectPtr<const USoundControlBusMix> BusMixPtr = InMix;
			RunCommandOnAudioThread([this, BusMixPtr]()
			{
				if (BusMixPtr.IsValid())
				{
					const USoundControlBusMix& Mix = *BusMixPtr.Get();
					FBusMixHandle BusMixHandle = FBusMixHandle::Get(Mix, RefProxies.BusMixes);
					if (BusMixHandle.IsValid())
					{
						BusMixHandle.FindProxy() = Mix;
					}
					else
					{
						UE_LOG(LogAudioModulation, Verbose, TEXT("Update to '%s' Ignored: Control Bus Mix is inactive."), *Mix.GetName());
					}
				}
			});
		}

		if (const USoundModulationPatch* InPatch = Cast<USoundModulationPatch>(&InModulator))
		{
			const TWeakObjectPtr<const USoundModulationPatch> PatchPtr = InPatch;
			RunCommandOnAudioThread([this, PatchPtr]()
			{
				if (PatchPtr.IsValid())
				{
					const USoundModulationPatch& Patch = *PatchPtr.Get();
					FPatchHandle PatchHandle = FPatchHandle::Get(Patch, RefProxies.Patches);
					if (PatchHandle.IsValid())
					{
						FModulationPatchRefProxy& PatchProxy = PatchHandle.FindProxy();
						PatchProxy = Patch;
					}
					else
					{
						UE_LOG(LogAudioModulation, Verbose, TEXT("Update to '%s' Ignored: Patch is inactive."), *Patch.GetName());
					}
				}
			});
		}
	}
} // namespace AudioModulation
#endif // WITH_AUDIOMODULATION
