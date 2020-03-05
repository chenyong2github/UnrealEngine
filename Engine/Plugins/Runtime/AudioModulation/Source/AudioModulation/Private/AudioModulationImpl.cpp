// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioModulationInternal.h"

#if WITH_AUDIOMODULATION
#include "Audio/AudioAddressPattern.h"
#include "AudioModulationLogging.h"
#include "AudioModulationProfileSerializer.h"
#include "AudioThread.h"
#include "Engine/Engine.h"
#include "IAudioExtensionPlugin.h"
#include "Misc/CoreDelegates.h"
#include "SoundControlBusMix.h"
#include "SoundModulatorLFO.h"
#include "SoundModulationPatch.h"
#include "SoundModulationProxy.h"
#include "SoundModulationValue.h"
#include "UObject/UObjectIterator.h"
#include "UObject/WeakObjectPtr.h"


#if !UE_BUILD_SHIPPING
#include "AudioModulationDebugger.h"
#include "Async/Async.h"
#endif // !UE_BUILD_SHIPPING

DECLARE_DWORD_COUNTER_STAT(TEXT("Bus Count"), STAT_AudioModulationBusCount, STATGROUP_AudioModulation)
DECLARE_DWORD_COUNTER_STAT(TEXT("LFO Count"), STAT_AudioModulationLFOCount, STATGROUP_AudioModulation)
DECLARE_DWORD_COUNTER_STAT(TEXT("Mix Count"), STAT_AudioModulationMixCount, STATGROUP_AudioModulation)


namespace AudioModulation
{
	void MixInModulationValue(ESoundModulatorOperator& Operator, float ModStageValue, float& Value)
	{
		switch (Operator)
		{
		case ESoundModulatorOperator::Max:
		{
			Value = FMath::Max(ModStageValue, Value);
		}
		break;

		case ESoundModulatorOperator::Min:
		{
			Value = FMath::Min(ModStageValue, Value);
		}
		break;

		case ESoundModulatorOperator::Multiply:
		default:
		{
			Value *= ModStageValue;
			static_assert(static_cast<int32>(ESoundModulatorOperator::Count) == 3, "Possible missing operator switch case coverage");
		}
		break;
		}
	}

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

	FAudioModulationImpl::FAudioModulationImpl()
	{
	}

	void FAudioModulationImpl::Initialize(const FAudioPluginInitializationParams& InitializationParams)
	{
		SourceSettings.AddDefaulted(InitializationParams.NumSources);
	}

#if WITH_EDITOR
	void FAudioModulationImpl::OnEditPluginSettings(const USoundModulationPluginSourceSettingsBase& InSettings)
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
					SourceSetting = FModulationSettingsProxy(*Settings, RefProxies, *this);
				}
			}

			for (TPair<uint32, FModulationSettingsProxy>& Pair : SoundSettings)
			{
				if (Pair.Value.GetId() == SettingsId)
				{
					Pair.Value = FModulationSettingsProxy(*Settings, RefProxies, *this);
				}
			}
		});
	}
#endif // WITH_EDITOR

	void FAudioModulationImpl::OnInitSound(ISoundModulatable& InSound, const USoundModulationPluginSourceSettingsBase& InSettings)
	{
		check(IsInAudioThread());

		const uint32 SoundId = InSound.GetObjectId();
		if (!SoundSettings.Contains(SoundId))
		{
			const USoundModulationSettings* Settings = CastChecked<USoundModulationSettings>(&InSettings);
			SoundSettings.Add(SoundId, FModulationSettingsProxy(*Settings, RefProxies, *this));
		}
	}

	void FAudioModulationImpl::OnInitSource(const uint32 InSourceId, const FName& AudioComponentUserId, const uint32 NumChannels, const USoundModulationPluginSourceSettingsBase& InSettings)
	{
		check(IsInAudioThread());

		if (const USoundModulationSettings* Settings = CastChecked<USoundModulationSettings>(&InSettings))
		{
			SourceSettings[InSourceId] = FModulationSettingsProxy(*Settings, RefProxies, *this);
		}
	}

	void FAudioModulationImpl::OnReleaseSource(const uint32 InSourceId)
	{
		check(IsInAudioThread());

		SourceSettings[InSourceId] = FModulationSettingsProxy();
	}

	void FAudioModulationImpl::OnReleaseSound(ISoundModulatable& InSound)
	{
		check(IsInAudioThread());
		check(InSound.GetObjectId() != INDEX_NONE);

		if (InSound.GetPlayCount() == 0)
		{
			SoundSettings.Remove(InSound.GetObjectId());
		}
	}

#if !UE_BUILD_SHIPPING
	bool FAudioModulationImpl::OnPostHelp(FCommonViewportClient* ViewportClient, const TCHAR* Stream)
	{
		return ViewportClient ? Debugger.OnPostHelp(*ViewportClient, Stream) : true;
	}

	int32 FAudioModulationImpl::OnRenderStat(FViewport* Viewport, FCanvas* Canvas, int32 X, int32 Y, const UFont& Font, const FVector* ViewLocation, const FRotator* ViewRotation)
	{
		return Canvas ? Debugger.OnRenderStat(*Canvas, X, Y, Font) : Y;
	}

	bool FAudioModulationImpl::OnToggleStat(FCommonViewportClient* ViewportClient, const TCHAR* Stream)
	{
		return ViewportClient ? Debugger.OnToggleStat(*ViewportClient, Stream) : true;
	}
#endif // !UE_BUILD_SHIPPING

	void FAudioModulationImpl::ActivateBus(const USoundControlBusBase& InBus)
	{
		const TWeakObjectPtr<const USoundControlBusBase>BusPtr = &InBus;
		RunCommandOnAudioThread([this, BusPtr]()
		{
			if (BusPtr.IsValid())
			{
				auto OnCreate = [this, BusPtr](FControlBusProxy& NewProxy)
				{
					NewProxy.InitLFOs(*BusPtr.Get(), RefProxies.LFOs);
				};

				FBusHandle BusHandle = FBusHandle::Create(*BusPtr.Get(), RefProxies.Buses, *this, OnCreate);
				ManuallyActivatedBuses.Add(MoveTemp(BusHandle));
			}
		});

	}

	void FAudioModulationImpl::ActivateBusMix(const USoundControlBusMix& InBusMix)
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

	void FAudioModulationImpl::ActivateLFO(const USoundBusModulatorLFO& InLFO)
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

	float FAudioModulationImpl::CalculateModulationValue(FModulationPatchProxy& OutProxy) const
	{
		float OutValue = OutProxy.DefaultInputValue;

		float& OutSampleHold = OutProxy.OutputProxy.SampleAndHoldValue;
		if (!OutProxy.OutputProxy.bInitialized)
		{
			switch (OutProxy.OutputProxy.Operator)
			{
				case ESoundModulatorOperator::Max:
				{
					OutSampleHold = OutProxy.OutputProxy.Transform.OutputMin;
				}
				break;

				case ESoundModulatorOperator::Min:
				{
					OutSampleHold = OutProxy.OutputProxy.Transform.OutputMax;
				}
				break;

				case ESoundModulatorOperator::Multiply:
				{
					OutSampleHold = 1.0f;
				}
				break;

				default:
				{
					static_assert(static_cast<int32>(ESoundModulatorOperator::Count) == 3, "Possible missing operator switch case coverage");
				}
				break;
			}
		}

		for (const FModulationInputProxy& InputProxy : OutProxy.InputProxies)
		{
			if (InputProxy.bSampleAndHold)
			{
				if (!OutProxy.OutputProxy.bInitialized && InputProxy.BusHandle.IsValid())
				{
					const FControlBusProxy& BusProxy = InputProxy.BusHandle.FindProxy();
					if (!BusProxy.IsBypassed())
					{
						float ModStageValue = BusProxy.GetValue();
						InputProxy.Transform.Apply(ModStageValue);
						MixInModulationValue(OutProxy.OutputProxy.Operator, ModStageValue, OutSampleHold);
					}
				}
			}
			else
			{
				if (InputProxy.BusHandle.IsValid())
				{
					const FControlBusProxy& BusProxy = InputProxy.BusHandle.FindProxy();
					if (!BusProxy.IsBypassed())
					{
						float ModStageValue = BusProxy.GetValue();
						InputProxy.Transform.Apply(ModStageValue);
						MixInModulationValue(OutProxy.OutputProxy.Operator, ModStageValue, OutValue);
					}
				}
			}
		}

		if (!OutProxy.OutputProxy.bInitialized)
		{
			const float OutputMin = OutProxy.OutputProxy.Transform.OutputMin;
			const float OutputMax = OutProxy.OutputProxy.Transform.OutputMax;
			OutSampleHold = FMath::Clamp(OutSampleHold, OutputMin, OutputMax);
			OutProxy.OutputProxy.bInitialized = true;
		}

		OutProxy.OutputProxy.Transform.Apply(OutValue);
		MixInModulationValue(OutProxy.OutputProxy.Operator, OutSampleHold, OutValue);
		return OutValue;
	}

	bool FAudioModulationImpl::CalculateModulationValue(FModulationPatchProxy& OutProxy, float& OutValue) const
	{
		if (OutProxy.bBypass)
		{
			return false;
		}

		const float InitValue = OutValue;
		OutValue = CalculateModulationValue(OutProxy);

		return !FMath::IsNearlyEqual(InitValue, OutValue);
	}

	float FAudioModulationImpl::CalculateInitialVolume(const USoundModulationPluginSourceSettingsBase& SettingsBase)
	{
		check(IsInAudioThread());

		const USoundModulationSettings* Settings = CastChecked<USoundModulationSettings>(&SettingsBase);
		FModulationPatchProxy VolumePatch(Settings->Volume, RefProxies, *this);

		return CalculateModulationValue(VolumePatch);
	}

	void FAudioModulationImpl::DeactivateBus(const USoundControlBusBase& InBus)
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

	void FAudioModulationImpl::DeactivateBusMix(const USoundControlBusMix& InBusMix)
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

	void FAudioModulationImpl::DeactivateLFO(const USoundBusModulatorLFO& InLFO)
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

	bool FAudioModulationImpl::IsBusActive(const FBusId InBusId) const
	{
		check(IsInAudioThread());

		return RefProxies.Buses.Contains(InBusId);
	}

	bool FAudioModulationImpl::ProcessControls(const uint32 InSourceId, FSoundModulationControls& OutControls)
	{
		check(IsInAudioThread());

		bool bControlsUpdated = false;

		FModulationSettingsProxy& Settings = SourceSettings[InSourceId];

		if (Settings.Volume.bBypass)
		{
			OutControls.Volume = 1.0f;
		}
		else
		{
			bControlsUpdated |= CalculateModulationValue(Settings.Volume, OutControls.Volume);
		}

		if (Settings.Pitch.bBypass)
		{
			OutControls.Pitch = 1.0f;
		}
		else
		{
			bControlsUpdated |= CalculateModulationValue(Settings.Pitch, OutControls.Pitch);
		}

		if (Settings.Highpass.bBypass)
		{
			OutControls.Highpass = MIN_FILTER_FREQUENCY;
		}
		else
		{
			bControlsUpdated |= CalculateModulationValue(Settings.Highpass, OutControls.Highpass);
		}

		if (Settings.Lowpass.bBypass)
		{
			OutControls.Lowpass = MAX_FILTER_FREQUENCY;
		}
		else
		{
			bControlsUpdated |= CalculateModulationValue(Settings.Lowpass, OutControls.Lowpass);
		}

		for (TPair<FName, FModulationPatchProxy>& Pair : Settings.Controls)
		{
			if (!Pair.Value.bBypass)
			{
				float& OutputValue = OutControls.Controls.FindOrAdd(Pair.Key);
				bControlsUpdated |= CalculateModulationValue(Pair.Value, OutputValue);
			}
		}

		return bControlsUpdated;
	}

	void FAudioModulationImpl::ProcessModulators(float Elapsed)
	{
		check(IsInAudioThread());

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

		SET_DWORD_STAT(STAT_AudioModulationBusCount, RefProxies.Buses.Num());
		SET_DWORD_STAT(STAT_AudioModulationMixCount, RefProxies.BusMixes.Num());
		SET_DWORD_STAT(STAT_AudioModulationLFOCount, RefProxies.LFOs.Num());

#if !UE_BUILD_SHIPPING
		Debugger.UpdateDebugData(RefProxies);
#endif // !UE_BUILD_SHIPPING
	}

	void FAudioModulationImpl::SaveMixToProfile(const USoundControlBusMix& InBusMix, const int32 InProfileIndex)
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

	TArray<FSoundControlBusMixChannel> FAudioModulationImpl::LoadMixFromProfile(const int32 InProfileIndex, USoundControlBusMix& OutBusMix)
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

	void FAudioModulationImpl::RunCommandOnAudioThread(TFunction<void()> Cmd)
	{
		if (IsInAudioThread())
		{
			Cmd();
		}
		else
		{
			FAudioThread::RunCommandOnAudioThread(Cmd);
		}
	}

	void FAudioModulationImpl::UpdateMix(const TArray<FSoundControlBusMixChannel>& InChannels, USoundControlBusMix& InOutMix, bool bInUpdateObject)
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

	void FAudioModulationImpl::UpdateMixByFilter(
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

	void FAudioModulationImpl::UpdateModulator(const USoundModulatorBase& InModulator)
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
						Proxy.InitLFOs(*Bus, RefProxies.LFOs);
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
	}
} // namespace AudioModulation
#endif // WITH_AUDIOMODULATION
