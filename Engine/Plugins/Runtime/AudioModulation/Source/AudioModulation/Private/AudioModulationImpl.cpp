// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "AudioModulationInternal.h"

#if WITH_AUDIOMODULATION
#include "AudioModulationLogging.h"
#include "AudioThread.h"
#include "Engine/Engine.h"
#include "IAudioExtensionPlugin.h"
#include "Misc/CoreDelegates.h"
#include "SoundModulatorLFO.h"
#include "SoundModulationPatch.h"
#include "SoundModulationValue.h"

#if !UE_BUILD_SHIPPING
#include "AudioModulationDebugger.h"
#endif // !UE_BUILD_SHIPPING

DECLARE_DWORD_COUNTER_STAT(TEXT("Bus Count"), STAT_AudioModulationBusCount, STATGROUP_AudioModulation)
DECLARE_DWORD_COUNTER_STAT(TEXT("LFO Count"), STAT_AudioModulationLFOCount, STATGROUP_AudioModulation)
DECLARE_DWORD_COUNTER_STAT(TEXT("Mix Count"), STAT_AudioModulationMixCount, STATGROUP_AudioModulation)


namespace
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
} // namespace <>


namespace AudioModulation
{
	void FAudioModulationImpl::Initialize(const FAudioPluginInitializationParams& InitializationParams)
	{
		SourceSettings.AddDefaulted(InitializationParams.NumSources);
	}

#if WITH_EDITOR
	void FAudioModulationImpl::OnEditSource(const USoundModulationPluginSourceSettingsBase& Settings)
	{
		FModulationSettingsProxy NewProxy(*CastChecked<USoundModulationSettings>(&Settings));
		const uint32 ObjectID = Settings.GetUniqueID();

		auto EditSource = [this, NewProxy, ObjectID]()
		{
			for (FModulationSettingsProxy& CurrentSettingsProxy : SourceSettings)
			{
				if (CurrentSettingsProxy.ObjectID == ObjectID)
				{
					CurrentSettingsProxy = NewProxy;
				}
			}
		};

		IsInAudioThread() ? EditSource() : FAudioThread::RunCommandOnAudioThread(EditSource);
	}
#endif // WITH_EDITOR

	void FAudioModulationImpl::OnInitSound(const ModulationSoundId SoundId, const USoundModulationPluginSourceSettingsBase& InSettings)
	{
		check(IsInAudioThread());

		if (const USoundModulationSettings* Settings = CastChecked<USoundModulationSettings>(&InSettings))
		{
			for (const USoundModulatorBusMix* Mix : Settings->Mixes)
			{
				if (Mix)
				{
					ActivateBusMix(*Mix, /* bReset */ false, SoundId);
				}
			}

			auto CheckRefActivate = [this](const USoundModulatorBusBase* Bus)
			{
				if (!Bus)
				{
					return;
				}

				if (Bus->bAutoActivate)
				{
					ActivateBus(*Bus);
				}

				for (const USoundModulatorBase* Modulator : Bus->Modulators)
				{
					if (const USoundModulatorLFO* LFO = Cast<USoundModulatorLFO>(Modulator))
					{
						if (LFO->bAutoActivate)
						{
							FModulatorLFOProxy LFOProxy(*LFO);
							ActivateLFO(*LFO);
						}
					}
				}
			};

			for (const FSoundVolumeModulationInput& Input : Settings->Volume.Inputs)
			{
				CheckRefActivate(Cast<USoundModulatorBusBase>(Input.Bus));
			}

			for (const FSoundPitchModulationInput& Input : Settings->Pitch.Inputs)
			{
				CheckRefActivate(Cast<USoundModulatorBusBase>(Input.Bus));
			}

			for (const FSoundLPFModulationInput& Input : Settings->Lowpass.Inputs)
			{
				CheckRefActivate(Cast<USoundModulatorBusBase>(Input.Bus));
			}

			for (const FSoundHPFModulationInput& Input : Settings->Highpass.Inputs)
			{
				CheckRefActivate(Cast<USoundModulatorBusBase>(Input.Bus));
			}
		}
	}

	void FAudioModulationImpl::OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, const uint32 NumChannels, const USoundModulationPluginSourceSettingsBase& InSettings)
	{
		check(IsInAudioThread());

		if (const USoundModulationSettings* Settings = CastChecked<USoundModulationSettings>(&InSettings))
		{
			SourceSettings[SourceId] = FModulationSettingsProxy(*Settings);
		}
	}

	void FAudioModulationImpl::OnReleaseSource(const uint32 SourceId)
	{
		check(IsInAudioThread());

		SourceSettings[SourceId] = FModulationSettingsProxy();
	}

	void FAudioModulationImpl::OnReleaseSound(const ModulationSoundId SoundId, const USoundModulationPluginSourceSettingsBase& Settings)
	{
		check(IsInAudioThread());

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

	void FAudioModulationImpl::ActivateBus(const USoundModulatorBusBase& Bus, const ModulationSoundId SoundId)
	{
		const FModulatorBusProxy NewBusProxy(Bus);

		auto ActivateBusInternal = [this, NewBusProxy, SoundId]()
		{
			const BusId NewBusId = NewBusProxy.GetBusId();
			FModulatorBusProxy* BusProxy = ActiveBuses.Find(NewBusId);
			if (!BusProxy)
			{
				BusProxy = &ActiveBuses.Add(NewBusId, NewBusProxy);
			}

			if (BusProxy->GetAutoActivate() && SoundId != INDEX_NONE)
			{
				BusProxy->IncRefSound();
			}
		};

		IsInAudioThread() ? ActivateBusInternal() : FAudioThread::RunCommandOnAudioThread(ActivateBusInternal);
	}

	void FAudioModulationImpl::ActivateBusMix(const USoundModulatorBusMix& BusMix, bool bReset, const ModulationSoundId SoundId)
	{
		const BusMixId MixId = static_cast<BusMixId>(BusMix.GetUniqueID());
		FModulatorBusMixProxy MixProxy(BusMix);

		auto ActivateMixInternal = [this, MixId, MixProxy, bReset, SoundId]()
		{
			FModulatorBusMixProxy* BusMixProxy = nullptr;
			if (FModulatorBusMixProxy* ExistingMixProxy = ActiveBusMixes.Find(MixId))
			{
				// Enable in case mix is currently stopping but not yet stopped.
				ExistingMixProxy->SetEnabled();
				if (bReset)
				{
					for (int32 i = 0; i < ExistingMixProxy->Channels.Num(); ++i)
					{
						if (ExistingMixProxy->Channels[i].BusId == MixProxy.Channels[i].BusId)
						{
							ExistingMixProxy->Channels[i].Value.TargetValue = MixProxy.Channels[i].Value.TargetValue;
						}
					}
				}

				BusMixProxy = ExistingMixProxy;
			}
			else
			{
				BusMixProxy = &ActiveBusMixes.Emplace(MixId, MixProxy);
			}

			if (BusMixProxy->GetAutoActivate() && SoundId != INDEX_NONE)
			{
				BusMixProxy->IncRefSound();
			}
		};

		IsInAudioThread() ? ActivateMixInternal() : FAudioThread::RunCommandOnAudioThread(ActivateMixInternal);
	}

	void FAudioModulationImpl::ActivateLFO(const USoundModulatorLFO& LFO, const ModulationSoundId SoundId)
	{
		const AudioModulation::LFOId LFOId = static_cast<const AudioModulation::LFOId>(LFO.GetUniqueID());
		const FModulatorLFOProxy NewLFOProxy(LFO);

		auto ActivateLFOInternal = [this, LFOId, NewLFOProxy, SoundId]()
		{
			FModulatorLFOProxy* LFOProxy = nullptr;
			if (!LFOProxy)
			{
				LFOProxy = &ActiveLFOs.Add(LFOId, NewLFOProxy);
			}

			if (SoundId != INDEX_NONE)
			{
				LFOProxy->IncRefSound();
			}
		};

		IsInAudioThread() ? ActivateLFOInternal() : FAudioThread::RunCommandOnAudioThread(ActivateLFOInternal);
	}

	float FAudioModulationImpl::CalculateModulationValue(FModulationPatchProxy& Proxy) const
	{
		float Value = Proxy.DefaultInputValue;

		if (!Proxy.OutputProxy.bInitialized)
		{
			Proxy.OutputProxy.SampleAndHoldValue = Proxy.DefaultInputValue;
		}

		for (const FModulationInputProxy& InputProxy : Proxy.InputProxies)
		{
			if (InputProxy.bSampleAndHold)
			{
				if (!Proxy.OutputProxy.bInitialized)
				{
					if (const FModulatorBusProxy* BusProxy = ActiveBuses.Find(InputProxy.BusId))
					{
						float ModStageValue = BusProxy->GetValue();
						InputProxy.Transform.Apply(ModStageValue);
						MixInModulationValue(Proxy.OutputProxy.Operator, ModStageValue, Proxy.OutputProxy.SampleAndHoldValue);
					}
				}
			}
			else
			{
				if (const FModulatorBusProxy* BusProxy = ActiveBuses.Find(InputProxy.BusId))
				{
					float ModStageValue = BusProxy->GetValue();
					InputProxy.Transform.Apply(ModStageValue);
					MixInModulationValue(Proxy.OutputProxy.Operator, ModStageValue, Value);
				}
			}
		}

		Proxy.OutputProxy.bInitialized = true;
		MixInModulationValue(Proxy.OutputProxy.Operator, Proxy.OutputProxy.SampleAndHoldValue, Value);
		Proxy.OutputProxy.Transform.Apply(Value);
		return Value;
	}

	void FAudioModulationImpl::DeactivateBusMix(const BusMixId BusMixId, const ModulationSoundId SoundId)
	{
		auto DeactivateMix = [this, BusMixId, SoundId]()
		{
			check(IsInAudioThread());

			if (FModulatorBusMixProxy* Mix = ActiveBusMixes.Find(BusMixId))
			{
				if (SoundId == INDEX_NONE)
				{
					Mix->SetStopping();
				}
				else if (Mix->GetAutoActivate())
				{
					if (Mix->DecRefSound() == 0)
					{
						Mix->SetStopping();
					}
				}
			}
		};

		IsInAudioThread() ? DeactivateMix() : FAudioThread::RunCommandOnAudioThread(DeactivateMix);
	}

	void FAudioModulationImpl::DeactivateBus(const BusId BusId, const ModulationSoundId SoundId)
	{
		auto Deactivate = [this, BusId, SoundId]()
		{
			check(IsInAudioThread());

			if (FModulatorBusProxy* Bus = ActiveBuses.Find(BusId))
			{
				if (SoundId == INDEX_NONE)
				{
					ActiveBuses.Remove(BusId);
				}
				else if (Bus->GetAutoActivate())
				{
					if (Bus->DecRefSound() == 0)
					{
						ActiveBuses.Remove(BusId);
					}
				}
			}
		};

		IsInAudioThread() ? Deactivate() : FAudioThread::RunCommandOnAudioThread(Deactivate);
	}

	void FAudioModulationImpl::DeactivateLFO(const LFOId LFOId, const ModulationSoundId SoundId)
	{
		if (!IsInAudioThread())
		{
			FAudioThread::RunCommandOnAudioThread([this, LFOId, SoundId]()
			{
				DeactivateLFO(LFOId);
			});

			return;
		}

		ActiveLFOs.Remove(LFOId);
	}

	bool FAudioModulationImpl::IsBusActive(const BusId BusId) const
	{
		check(IsInAudioThread());

		return ActiveBuses.Contains(BusId);
	}

	void FAudioModulationImpl::ProcessControls(const uint32 SourceId, FSoundModulationControls& Controls)
	{
		check(IsInAudioThread());

		FModulationSettingsProxy& Settings = SourceSettings[SourceId];

		Controls.Volume   = CalculateModulationValue(Settings.Volume);
		Controls.Pitch    = CalculateModulationValue(Settings.Pitch);
		Controls.Lowpass  = CalculateModulationValue(Settings.Lowpass);
		Controls.Highpass = CalculateModulationValue(Settings.Highpass);
	}

	void FAudioModulationImpl::ProcessModulators(float Elapsed)
	{
		check(IsInAudioThread());

		// Update LFOs (prior to bus mixing to avoid single-frame latency)
		for (TPair<LFOId, FModulatorLFOProxy>& Pair : ActiveLFOs)
		{
			Pair.Value.Update(Elapsed);
		}

		// Reset buses & refresh cached LFO
		for (TPair<BusId, FModulatorBusProxy>& Pair : ActiveBuses)
		{
			Pair.Value.Reset();
			Pair.Value.MixLFO(ActiveLFOs);
		}

		// Update mix values and apply to prescribed buses.
		// Track bus mixes ready to remove
		TArray<BusMixId> MixesToDeactivate;
		for (TPair<BusMixId, FModulatorBusMixProxy>& Pair : ActiveBusMixes)
		{
			Pair.Value.Update(Elapsed, ActiveBuses);
			if (Pair.Value.CanDestroy())
			{
				UE_LOG(LogAudioModulation, Log, TEXT("Audio modulation mix '%u' stopped."), static_cast<uint32>(Pair.Key));
				MixesToDeactivate.Add(Pair.Key);
			}
		}

		// Destroy mixes that have stopped
		for (const BusMixId& MixId : MixesToDeactivate)
		{
			ActiveBusMixes.Remove(MixId);
		}

		SET_DWORD_STAT(STAT_AudioModulationBusCount, ActiveBuses.Num());
		SET_DWORD_STAT(STAT_AudioModulationMixCount, ActiveBusMixes.Num());
		SET_DWORD_STAT(STAT_AudioModulationLFOCount, ActiveLFOs.Num());

#if !UE_BUILD_SHIPPING
		Debugger.UpdateDebugData(ActiveBuses, ActiveBusMixes, ActiveLFOs);
#endif // !UE_BUILD_SHIPPING
	}

	void FAudioModulationImpl::SetBusDefault(const USoundModulatorBusBase& Bus, const float Value)
	{
		auto BusId = static_cast<const AudioModulation::BusId>(Bus.GetUniqueID());
		if (FModulatorBusProxy* BusProxy = ActiveBuses.Find(BusId))
		{
			BusProxy->SetDefaultValue(Value);
		}
	}

	void FAudioModulationImpl::SetBusRange(const USoundModulatorBusBase& Bus, const FVector2D& Range)
	{
		auto BusId = static_cast<const AudioModulation::BusId>(Bus.GetUniqueID());
		if (FModulatorBusProxy* BusProxy = ActiveBuses.Find(BusId))
		{
			BusProxy->SetRange(Range);
		}
	}

	void FAudioModulationImpl::SetBusMixChannel(const USoundModulatorBusMix& BusMix, const USoundModulatorBusBase& Bus, const float TargetValue)
	{
		const AudioModulation::BusMixId MixId = static_cast<const AudioModulation::BusMixId>(BusMix.GetUniqueID());
		const AudioModulation::BusId BusId = static_cast<const AudioModulation::BusId>(Bus.GetUniqueID());

		auto SetChannel = [this, MixId, BusId, TargetValue]()
		{
			if (FModulatorBusMixProxy* BusMixProxy = ActiveBusMixes.Find(MixId))
			{
				if (FModulatorBusMixChannelProxy* ChannelProxy = BusMixProxy->Channels.Find(BusId))
				{
					ChannelProxy->Value.TargetValue = TargetValue;
				}
			}
		};

		IsInAudioThread() ? SetChannel() : FAudioThread::RunCommandOnAudioThread(SetChannel);
	}

	void FAudioModulationImpl::SetLFOFrequency(const USoundModulatorLFO& LFO, const float Freq)
	{
		const AudioModulation::LFOId LFOId = static_cast<const AudioModulation::LFOId>(LFO.GetUniqueID());

		auto SetFreq = [this, LFOId, Freq]()
		{
			if (FModulatorLFOProxy* LFOProxy = ActiveLFOs.Find(LFOId))
			{
				LFOProxy->SetFreq(Freq);
			}
		};

		IsInAudioThread() ? SetFreq() : FAudioThread::RunCommandOnAudioThread(SetFreq);
	}
} // namespace AudioModulation
#endif // WITH_AUDIOMODULATION
