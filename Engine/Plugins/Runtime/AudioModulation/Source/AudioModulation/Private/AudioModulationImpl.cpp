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


namespace AudioModulation
{
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

	void FAudioModulationImpl::OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, const uint32 NumChannels, USoundModulationPluginSourceSettingsBase* InSettings)
	{
		check(IsInAudioThread());

		if (USoundModulationSettings* Settings = CastChecked<USoundModulationSettings>(InSettings))
		{
			SourceSettings[SourceId] = FModulationSettingsProxy(*Settings);
			for (const USoundModulatorBusMix* Mix : Settings->Mixes)
			{
				if (Mix)
				{
					ActivateBusMix(*Mix, /* bReset */ false);
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

	void FAudioModulationImpl::OnReleaseSource(const uint32 SourceId)
	{
		check(IsInAudioThread());

		SourceSettings[SourceId] = FModulationSettingsProxy();
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

	void FAudioModulationImpl::ActivateBus(const USoundModulatorBusBase& Bus)
	{
		FModulatorBusProxy BusProxy(Bus);
		ActivateBus(BusProxy);
	}

	void FAudioModulationImpl::ActivateBus(const FModulatorBusProxy& BusProxy)
	{
		auto ActivateBusInternal = [this, BusProxy]()
		{
			const BusId NewBusId = BusProxy.GetBusId();
			if (!ActiveBuses.Contains(NewBusId))
			{
				ActiveBuses.Add(NewBusId, BusProxy);
			}
		};

		IsInAudioThread() ? ActivateBusInternal() : FAudioThread::RunCommandOnAudioThread(ActivateBusInternal);
	}

	void FAudioModulationImpl::ActivateBusMix(const USoundModulatorBusMix& BusMix, bool bReset)
	{
		const BusMixId MixId = static_cast<BusMixId>(BusMix.GetUniqueID());
		FModulatorBusMixProxy MixProxy(BusMix);

		auto ActivateMixInternal = [this, MixId, MixProxy, bReset]()
		{
			FModulatorBusMixProxy* BusMixProxy = nullptr;
			if (FModulatorBusMixProxy* ExistingMixProxy = ActiveBusMixes.Find(MixId))
			{
				// Reactivate just in case it is currently stopping but not yet stopped.
				ExistingMixProxy->SetActive();
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
		};

		IsInAudioThread() ? ActivateMixInternal() : FAudioThread::RunCommandOnAudioThread(ActivateMixInternal);
	}

	void FAudioModulationImpl::ActivateLFO(const USoundModulatorLFO& LFO)
	{
		const AudioModulation::LFOId LFOId = static_cast<const AudioModulation::LFOId>(LFO.GetUniqueID());
		FModulatorLFOProxy LFOProxy(LFO);

		auto ActivateLFOInternal = [this, LFOId, LFOProxy]()
		{
			if (!ActiveLFOs.Contains(LFOId))
			{
				ActiveLFOs.Add(LFOId, LFOProxy);
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

	void FAudioModulationImpl::DeactivateBusMix(const BusMixId BusMixId)
	{
		auto DeactivateMix = [this, BusMixId]()
		{
			check(IsInAudioThread());

			if (FModulatorBusMixProxy* Mix = ActiveBusMixes.Find(BusMixId))
			{
				Mix->SetStopping();
			}
		};

		IsInAudioThread() ? DeactivateMix() : FAudioThread::RunCommandOnAudioThread(DeactivateMix);
	}

	void FAudioModulationImpl::DeactivateBus(const BusId BusId)
	{
		if (!IsInAudioThread())
		{
			FAudioThread::RunCommandOnAudioThread([this, BusId]()
			{
				DeactivateBus(BusId);
			});

			return;
		}

		ActiveBuses.Remove(BusId);
	}

	void FAudioModulationImpl::DeactivateLFO(const LFOId LFOId)
	{
		if (!IsInAudioThread())
		{
			FAudioThread::RunCommandOnAudioThread([this, LFOId]()
			{
				DeactivateLFO(LFOId);
			});

			return;
		}

		ActiveLFOs.Remove(LFOId);
	}

	TSet<BusId> FAudioModulationImpl::GetReferencedBusIds() const
	{
		auto GetActivePatchBusIds = [this](const FModulationPatchProxy& Patch, TSet<BusId>& OutBusIds)
		{
			for (const FModulationInputProxy& InputProxy : Patch.InputProxies)
			{
				if (InputProxy.bSampleAndHold)
				{
					if (!Patch.OutputProxy.bInitialized)
					{
						OutBusIds.Add(InputProxy.BusId);
					}
				}
				else
				{
					OutBusIds.Add(InputProxy.BusId);
				}
			}
		};

		TSet<BusId> BusIds;
		for (const FModulationSettingsProxy& Settings : SourceSettings)
		{
			GetActivePatchBusIds(Settings.Highpass, BusIds);
			GetActivePatchBusIds(Settings.Lowpass, BusIds);
			GetActivePatchBusIds(Settings.Pitch, BusIds);
			GetActivePatchBusIds(Settings.Volume, BusIds);
		}

		return MoveTemp(BusIds);
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
			// Clear is active as bus calls to MixLFO in subsequent loop will
			// mark as active.  If no buses are currently using the LFO and
			// its set to 'Limited Lifetime', then it will be marked to destroy.
			Pair.Value.ClearIsActive();
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
			if (Pair.Value.CanDeactivate())
			{
				UE_LOG(LogAudioModulation, Log, TEXT("Audio modulation mix '%u' stopped."), static_cast<uint32>(Pair.Key));
				MixesToDeactivate.Add(Pair.Key);
			}
		}

		{	// Destroy LFO proxies ready to deactivate
			TArray<LFOId> LFOsToDeactivate;
			for (TPair<LFOId, FModulatorLFOProxy>& Pair : ActiveLFOs)
			{
				if (Pair.Value.CanDeactivate())
				{
					LFOsToDeactivate.Add(Pair.Key);
				}
			}
			for (const LFOId& LFOId : LFOsToDeactivate)
			{
				DeactivateLFO(LFOId);
			}
		}

		{	// Destroy Bus proxies ready to deactivate
			const TSet<BusId> ReferencedBuses = GetReferencedBusIds();
			TArray<BusId> BusesToDeactivate;
			for (TPair<BusId, FModulatorBusProxy>& Pair : ActiveBuses)
			{
				FModulatorBusProxy& BusProxy = Pair.Value;
				if (BusProxy.CanDeactivate() && !ReferencedBuses.Contains(BusProxy.GetBusId()))
				{
					BusesToDeactivate.Add(Pair.Key);
				}
			}
			for (const BusId& BusId : BusesToDeactivate)
			{
				DeactivateBus(BusId);
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
