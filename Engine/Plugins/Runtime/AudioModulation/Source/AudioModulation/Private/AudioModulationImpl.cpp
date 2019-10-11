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
#if WITH_EDITOR
	// Checks whether new inputs being modified require stopping referencing preview sound for proxies to be updated and take change.
	template <typename T>
	bool InputUpdateRequiresStop(const TArray<T>& NewInputs, const TArray<AudioModulation::FModulationInputProxy>& CurrentInputProxies)
	{
		check(IsInAudioThread());

		if (NewInputs.Num() != CurrentInputProxies.Num())
		{
			return true;
		}

		for (int32 i = 0; i < NewInputs.Num(); ++i)
		{
			if (const USoundControlBusBase* NewBus = NewInputs[i].GetBus())
			{
				if (NewBus->GetUniqueID() != CurrentInputProxies[i].BusId)
				{
					return true;
				}
			}
			else
			{
				if (CurrentInputProxies[i].BusId != INDEX_NONE)
				{
					return true;
				}
			}
		}

		return false;
	};
#endif

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
	FAudioModulationImpl::FAudioModulationImpl()
#if WITH_EDITOR
		: PreviewSound(nullptr)
#endif // WITH_EDITOR
	{
	}

	void FAudioModulationImpl::Initialize(const FAudioPluginInitializationParams& InitializationParams)
	{
		SourceSettings.AddDefaulted(InitializationParams.NumSources);
	}

#if WITH_EDITOR
	void FAudioModulationImpl::OnEditPluginSettings(const USoundModulationPluginSourceSettingsBase& InSettings)
	{
		// Find if sound is being referenced and auditioned and stop immediately.
		// This informs user that modifying sound's settings does not translate to
		// currently playing sound.
		const uint32 SettingsId = InSettings.GetUniqueID();
		RunCommandOnAudioThread([this, SettingsId]()
		{
			if (PreviewSound)
			{
				USoundModulationPluginSourceSettingsBase* SettingsBase = PreviewSound->FindModulationSettings();
				if (SettingsBase && SettingsId == SettingsBase->GetUniqueID())
				{
					const USoundModulationSettings* Settings = CastChecked<USoundModulationSettings>(SettingsBase);

					bool bShouldStop = false;
					if (InputUpdateRequiresStop<FSoundVolumeModulationInput>(Settings->Volume.Inputs, PreviewSettings.Volume.InputProxies))
					{
						bShouldStop = true;
					}
					else if (InputUpdateRequiresStop<FSoundHPFModulationInput>(Settings->Highpass.Inputs, PreviewSettings.Highpass.InputProxies))
					{
						bShouldStop = true;
					}
					else if (InputUpdateRequiresStop<FSoundLPFModulationInput>(Settings->Lowpass.Inputs, PreviewSettings.Lowpass.InputProxies))
					{
						bShouldStop = true;
					}
					else if (InputUpdateRequiresStop<FSoundPitchModulationInput>(Settings->Pitch.Inputs, PreviewSettings.Pitch.InputProxies))
					{
						bShouldStop = true;
					}

					if (bShouldStop)
					{
						PreviewSound->Stop();
						PreviewSound = nullptr;
						PreviewSettings = FModulationSettingsProxy();
					}
					else
					{
						PreviewSettings = FModulationSettingsProxy(*Settings);
						for (FModulationSettingsProxy& SourceSetting : SourceSettings)
						{
							if (SourceSetting.GetId() == PreviewSettings.GetId())
							{
								SourceSetting = PreviewSettings;
							}
						}

						for (TPair<uint32, FModulationSettingsProxy>& Pair : SoundSettings)
						{
							if (Pair.Value.GetId() == PreviewSettings.GetId())
							{
								Pair.Value = PreviewSettings;
							}
						}
					}
				}
			}
		});
	}
#endif // WITH_EDITOR

	void FAudioModulationImpl::OnInitSound(ISoundModulatable& InSound, const USoundModulationPluginSourceSettingsBase& InSettings)
	{
		check(IsInAudioThread());

		const USoundModulationSettings* Settings = CastChecked<USoundModulationSettings>(&InSettings);

#if WITH_EDITOR
		if (InSound.IsPreviewSound())
		{
			PreviewSound = &InSound;
			PreviewSettings = FModulationSettingsProxy(*Settings);
		}
#endif // WITH_EDITOR

		const uint32 SoundId = InSound.GetObjectId();
		if (!SoundSettings.Contains(SoundId))
		{
			SoundSettings.Add(SoundId, FModulationSettingsProxy(*Settings));
		}

		for (const USoundControlBusMix* Mix : Settings->Mixes)
		{
			if (Mix && (Mix->bAutoActivate || InSound.IsPreviewSound()))
			{
				ActivateBusMix(*Mix, &InSound);
			}
		}

		auto CheckRefActivate = [this, &InSound](const USoundControlBusBase* Bus)
		{
			if (!Bus)
			{
				return;
			}

			if (Bus->bAutoActivate || InSound.IsPreviewSound())
			{
				ActivateBus(*Bus, &InSound);
			}

			for (const USoundBusModulatorBase* Modulator : Bus->Modulators)
			{
				if (const USoundBusModulatorLFO* LFO = Cast<USoundBusModulatorLFO>(Modulator))
				{
					if (LFO->bAutoActivate || InSound.IsPreviewSound())
					{
						FModulatorLFOProxy LFOProxy(*LFO);
						ActivateLFO(*LFO, &InSound);
					}
				}
			}
		};

		for (const FSoundVolumeModulationInput& Input : Settings->Volume.Inputs)
		{
			CheckRefActivate(Cast<USoundControlBusBase>(Input.Bus));
		}

		for (const FSoundPitchModulationInput& Input : Settings->Pitch.Inputs)
		{
			CheckRefActivate(Cast<USoundControlBusBase>(Input.Bus));
		}

		for (const FSoundLPFModulationInput& Input : Settings->Lowpass.Inputs)
		{
			CheckRefActivate(Cast<USoundControlBusBase>(Input.Bus));
		}

		for (const FSoundHPFModulationInput& Input : Settings->Highpass.Inputs)
		{
			CheckRefActivate(Cast<USoundControlBusBase>(Input.Bus));
		}
	}

	void FAudioModulationImpl::OnInitSource(const uint32 InSourceId, const FName& AudioComponentUserId, const uint32 NumChannels, const USoundModulationPluginSourceSettingsBase& InSettings)
	{
		check(IsInAudioThread());

		if (const USoundModulationSettings* Settings = CastChecked<USoundModulationSettings>(&InSettings))
		{
			SourceSettings[InSourceId] = FModulationSettingsProxy(*Settings);
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

		// Settings can be null if sound settings were modified via the editor while auditioning or in PIE
		if (const FModulationSettingsProxy* Settings = SoundSettings.Find(InSound.GetObjectId()))
		{
			for (FBusMixId MixId : Settings->Mixes)
			{
				DeactivateBusMix(MixId, &InSound);
			}

			for (const FModulationInputProxy& Input : Settings->Volume.InputProxies)
			{
				DeactivateBus(Input.BusId, &InSound);
			}

			for (const FModulationInputProxy& Input : Settings->Pitch.InputProxies)
			{
				DeactivateBus(Input.BusId, &InSound);
			}

			for (const FModulationInputProxy& Input : Settings->Lowpass.InputProxies)
			{
				DeactivateBus(Input.BusId, &InSound);
			}

			for (const FModulationInputProxy& Input : Settings->Highpass.InputProxies)
			{
				DeactivateBus(Input.BusId, &InSound);
			}
		}

#if WITH_EDITOR
		if (InSound.IsPreviewSound() && &InSound == PreviewSound)
		{
			PreviewSound = nullptr;
			PreviewSettings = FModulationSettingsProxy();
		}
#endif // WITH_EDITOR
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

	void FAudioModulationImpl::ActivateBus(const USoundControlBusBase& InBus, const ISoundModulatable* InSound)
	{
		const FControlBusProxy NewBusProxy(InBus);
		const bool bCanCreateNew = InBus.CanAutoActivate(InSound);

		auto ActivateBusInternal = [this, NewBusProxy, InSound, bCanCreateNew]()
		{
			const FBusId NewBusId = NewBusProxy.GetId();
			FControlBusProxy* BusProxy = ActiveBuses.Find(NewBusId);
			if (!BusProxy && bCanCreateNew)
			{
				BusProxy = &ActiveBuses.Add(NewBusId, NewBusProxy);
			}

			if (BusProxy && InSound)
			{
				BusProxy->OnInitSound(*InSound);
			}
		};

		IsInAudioThread() ? ActivateBusInternal() : FAudioThread::RunCommandOnAudioThread(ActivateBusInternal);
	}

	void FAudioModulationImpl::ActivateBusMix(const USoundControlBusMix& InBusMix, const ISoundModulatable* InSound)
	{
		const FModulatorBusMixProxy NewMixProxy(InBusMix);
		const bool bCanCreateNew = InBusMix.CanAutoActivate(InSound);

		auto ActivateMixInternal = [this, NewMixProxy, InSound, bCanCreateNew]()
		{
			const FBusMixId MixId = static_cast<FBusMixId>(NewMixProxy.GetId());
			FModulatorBusMixProxy* BusMixProxy = ActiveBusMixes.Find(MixId);
			if (BusMixProxy)
			{
				// Enable in case mix is currently stopping but not yet stopped.
				BusMixProxy->SetEnabled();
			}
			else if (bCanCreateNew)
			{
				BusMixProxy = &ActiveBusMixes.Add(MixId, NewMixProxy);
			}

			if (BusMixProxy && InSound)
			{
				BusMixProxy->OnInitSound(*InSound);
			}
		};

		IsInAudioThread() ? ActivateMixInternal() : FAudioThread::RunCommandOnAudioThread(ActivateMixInternal);
	}

	void FAudioModulationImpl::ActivateLFO(const USoundBusModulatorLFO& InLFO, const ISoundModulatable* InSound)
	{
		const FModulatorLFOProxy NewLFOProxy(InLFO);
		const bool bCanCreateNew = InLFO.CanAutoActivate(InSound);

		auto ActivateLFOInternal = [this, NewLFOProxy, InSound, bCanCreateNew]()
		{
			const AudioModulation::FLFOId LFOId = NewLFOProxy.GetId();
			FModulatorLFOProxy* LFOProxy = ActiveLFOs.Find(LFOId);
			if (!LFOProxy && bCanCreateNew)
			{
				LFOProxy = &ActiveLFOs.Add(LFOId, NewLFOProxy);
			}

			if (LFOProxy && InSound)
			{
				LFOProxy->OnInitSound(*InSound);
			}
		};

		IsInAudioThread() ? ActivateLFOInternal() : FAudioThread::RunCommandOnAudioThread(ActivateLFOInternal);
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
				if (!OutProxy.OutputProxy.bInitialized)
				{
					if (const FControlBusProxy* BusProxy = ActiveBuses.Find(InputProxy.BusId))
					{
						float ModStageValue = BusProxy->GetValue();
						InputProxy.Transform.Apply(ModStageValue);
						MixInModulationValue(OutProxy.OutputProxy.Operator, ModStageValue, OutSampleHold);
					}
				}
			}
			else
			{
				if (const FControlBusProxy* BusProxy = ActiveBuses.Find(InputProxy.BusId))
				{
					float ModStageValue = BusProxy->GetValue();
					InputProxy.Transform.Apply(ModStageValue);
					MixInModulationValue(OutProxy.OutputProxy.Operator, ModStageValue, OutValue);
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

	float FAudioModulationImpl::CalculateInitialVolume(const USoundModulationPluginSourceSettingsBase& SettingsBase) const
	{
		check(IsInAudioThread());

		const USoundModulationSettings* Settings = CastChecked<USoundModulationSettings>(&SettingsBase);
		FModulationPatchProxy VolumePatch(Settings->Volume);

		return CalculateModulationValue(VolumePatch);
	}

	void FAudioModulationImpl::DeactivateBusMix(const FBusMixId InBusMixId, const ISoundModulatable* InSound)
	{
		RunCommandOnAudioThread([this, InBusMixId, InSound]()
		{
			check(IsInAudioThread());

			if (FModulatorBusMixProxy* Mix = ActiveBusMixes.Find(InBusMixId))
			{
				if (!InSound)
				{
					if (!Mix->GetAutoActivate())
					{
						Mix->SetStopping();
					}
				}
				else if (Mix->OnReleaseSound(*InSound) == 0)
				{
					if (Mix->GetAutoActivate())
					{
						Mix->SetStopping();
					}
				}
			}
		});
	}

	void FAudioModulationImpl::DeactivateBus(const FBusId InBusId, const ISoundModulatable* InSound)
	{
		RunCommandOnAudioThread([this, InBusId, InSound]()
		{
			check(IsInAudioThread());

			if (FControlBusProxy* Bus = ActiveBuses.Find(InBusId))
			{
				if (!InSound)
				{
					if (!Bus->GetAutoActivate())
					{
						ActiveBuses.Remove(InBusId);
					}
				}
				else if (Bus->OnReleaseSound(*InSound) == 0)
				{
					if (Bus->GetAutoActivate())
					{
						ActiveBuses.Remove(InBusId);
					}
				}

				// Only pass along to referenced LFOs if deactivating
				// via notification of sound release
				if (InSound)
				{
					for (FLFOId LFOId : Bus->GetLFOIds())
					{
						DeactivateLFO(LFOId, InSound);
					}
				}
			}
		});
	}

	void FAudioModulationImpl::DeactivateLFO(const FLFOId InLFOId, const ISoundModulatable* InSound)
	{
		RunCommandOnAudioThread([this, InLFOId, InSound]()
		{
			check(IsInAudioThread());

			if (FModulatorLFOProxy* LFO = ActiveLFOs.Find(InLFOId))
			{
				if (!InSound)
				{
					if (!LFO->GetAutoActivate())
					{
						ActiveBuses.Remove(InLFOId);
					}
				}
				else if (LFO->OnReleaseSound(*InSound) == 0)
				{
					if (LFO->GetAutoActivate())
					{
						ActiveBuses.Remove(InLFOId);
					}
				}
			}
		});
	}

	bool FAudioModulationImpl::IsBusActive(const FBusId InBusId) const
	{
		check(IsInAudioThread());

		return ActiveBuses.Contains(InBusId);
	}

	bool FAudioModulationImpl::IsLFOActive(const FLFOId InLFOId) const
	{
		check(IsInAudioThread());

		return ActiveLFOs.Contains(InLFOId);
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
		for (TPair<FLFOId, FModulatorLFOProxy>& Pair : ActiveLFOs)
		{
			Pair.Value.Update(Elapsed);
		}

		// Reset buses & refresh cached LFO
		for (TPair<FBusId, FControlBusProxy>& Pair : ActiveBuses)
		{
			Pair.Value.Reset();
			Pair.Value.MixLFO(ActiveLFOs);
		}

		// Update mix values and apply to prescribed buses.
		// Track bus mixes ready to remove
		TArray<FBusMixId> MixesToDeactivate;
		for (TPair<FBusMixId, FModulatorBusMixProxy>& Pair : ActiveBusMixes)
		{
			Pair.Value.Update(Elapsed, ActiveBuses);
			if (Pair.Value.CanDestroy())
			{
				UE_LOG(LogAudioModulation, Log, TEXT("Audio modulation mix '%s' stopped."), *Pair.Value.GetName());
				MixesToDeactivate.Add(Pair.Key);
			}
		}

		// Destroy mixes that have stopped
		for (const FBusMixId& MixId : MixesToDeactivate)
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

	void FAudioModulationImpl::UpdateMix(const USoundControlBusMix& InMix, const TArray<FSoundControlBusMixChannel>& InChannels)
	{
		const FBusMixId MixId = static_cast<FBusMixId>(InMix.GetUniqueID());
		RunCommandOnAudioThread([this, MixId, InChannels]()
		{
			if (FModulatorBusMixProxy* BusMixes = ActiveBusMixes.Find(MixId))
			{
				BusMixes->SetMix(InChannels);
			}
		});
	}

	void FAudioModulationImpl::UpdateMixByFilter(
		const USoundControlBusMix&					InMix,
		const FString&								InAddressFilter,
		const TSubclassOf<USoundControlBusBase>&	InBusClass,
		const FSoundModulationValue&				InValue)
	{
		const FString	AddressFilter	= InAddressFilter;
		const uint32	ClassId			= InBusClass ? InBusClass->GetUniqueID() : USoundControlBusBase::StaticClass()->GetUniqueID();
		const FBusMixId MixId			= static_cast<FBusMixId>(InMix.GetUniqueID());

		RunCommandOnAudioThread([this, ClassId, MixId, AddressFilter, InValue]()
		{
			if (FModulatorBusMixProxy* MixProxy = ActiveBusMixes.Find(MixId))
			{
				MixProxy->SetMixByFilter(AddressFilter, ClassId, InValue);
			}
		});
	}

	void FAudioModulationImpl::UpdateModulator(const USoundModulatorBase& InModulator)
	{
		if (const USoundBusModulatorLFO* LFO = Cast<USoundBusModulatorLFO>(&InModulator))
		{
			const FLFOId LFOId = static_cast<FLFOId>(InModulator.GetUniqueID());
			const FModulatorLFOProxy UpdateProxy(*LFO);
			RunCommandOnAudioThread([this, UpdateProxy, LFOId]()
			{
				if (FModulatorLFOProxy* LFOProxy = ActiveLFOs.Find(static_cast<FLFOId>(LFOId)))
				{
					LFOProxy->OnUpdateProxy(UpdateProxy);
				}
			});
		}

		if (const USoundControlBusBase* Bus = Cast<USoundControlBusBase>(&InModulator))
		{
			const FBusId BusId = static_cast<FBusId>(InModulator.GetUniqueID());
			const FControlBusProxy UpdateProxy(*Bus);
			RunCommandOnAudioThread([this, UpdateProxy, BusId]()
			{
				if (FControlBusProxy* BusProxy = ActiveBuses.Find(static_cast<FBusId>(BusId)))
				{
					BusProxy->OnUpdateProxy(UpdateProxy);
				}
			});
		}

		if (const USoundControlBusMix* Mix = Cast<USoundControlBusMix>(&InModulator))
		{
			const FBusMixId BusMixId = static_cast<FBusMixId>(InModulator.GetUniqueID());
			const FModulatorBusMixProxy UpdateProxy(*Mix);
			RunCommandOnAudioThread([this, UpdateProxy, BusMixId]()
			{
				if (FModulatorBusMixProxy* BusMixProxy = ActiveBusMixes.Find(static_cast<FBusMixId>(BusMixId)))
				{
					BusMixProxy->OnUpdateProxy(UpdateProxy);
				}
			});
		}
	}
} // namespace AudioModulation
#endif // WITH_AUDIOMODULATION
