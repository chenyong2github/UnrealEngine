// Copyright Epic Games, Inc. All Rights Reserved.
#include "ActiveSound.h"

#include "DrawDebugHelpers.h"
#include "EngineDefines.h"
#include "Misc/App.h"
#include "AudioThread.h"
#include "AudioDevice.h"
#include "IAudioExtensionPlugin.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundNodeAttenuation.h"
#include "Sound/SoundSubmix.h"
#include "SubtitleManager.h"


static int32 AudioOcclusionDisabledCvar = 0;
FAutoConsoleVariableRef CVarAudioOcclusionEnabled(
	TEXT("au.DisableOcclusion"),
	AudioOcclusionDisabledCvar,
	TEXT("Disables (1) or enables (0) audio occlusion.\n"),
	ECVF_Default);

FTraceDelegate FActiveSound::ActiveSoundTraceDelegate;
TMap<FTraceHandle, FActiveSound::FAsyncTraceDetails> FActiveSound::TraceToActiveSoundMap;

FActiveSound::FActiveSound()
	: World(nullptr)
	, WorldID(0)
	, Sound(nullptr)
	, SourceEffectChain(nullptr)
	, AudioComponentID(0)
	, OwnerID(0)
	, AudioDevice(nullptr)
	, SoundClassOverride(nullptr)
	, bHasCheckedOcclusion(false)
	, bAllowSpatialization(true)
	, bHasAttenuationSettings(false)
	, bShouldRemainActiveIfDropped(false)
	, bFinished(false)
	, bIsPaused(false)
	, bShouldStopDueToMaxConcurrency(false)
	, bHasVirtualized(false)
	, bRadioFilterSelected(false)
	, bApplyRadioFilter(false)
	, bHandleSubtitles(true)
	, bHasExternalSubtitles(false)
	, bLocationDefined(false)
	, bIgnoreForFlushing(false)
	, bAlwaysPlay(false)
	, bIsUISound(false)
	, bIsMusic(false)
	, bReverb(false)
	, bCenterChannelOnly(false)
	, bIsPreviewSound(false)
	, bGotInteriorSettings(false)
	, bApplyInteriorVolumes(false)
#if !(NO_LOGGING || UE_BUILD_SHIPPING || UE_BUILD_TEST)
	, bWarnedAboutOrphanedLooping(false)
#endif
	, bEnableLowPassFilter(false)
	, bUpdatePlayPercentage(false)
	, bUpdateSingleEnvelopeValue(false)
	, bUpdateMultiEnvelopeValue(false)
	, bUpdatePlaybackTime(false)
	, bIsPlayingAudio(false)
	, bIsStopping(false)
	, UserIndex(0)
	, FadeOut(EFadeOut::None)
	, bIsOccluded(false)
	, bAsyncOcclusionPending(false)
	, PlaybackTime(0.0f)
	, PlaybackTimeNonVirtualized(0.0f)
	, MinCurrentPitch(1.0f)
	, RequestedStartTime(0.0f)
	, VolumeMultiplier(1.0f)
	, PitchMultiplier(1.0f)
	, LowPassFilterFrequency(MAX_FILTER_FREQUENCY)
	, CurrentOcclusionFilterFrequency(MAX_FILTER_FREQUENCY)
	, CurrentOcclusionVolumeAttenuation(1.0f)
	, SubtitlePriority(DEFAULT_SUBTITLE_PRIORITY)
	, Priority(1.0f)
	, VolumeConcurrency(0.0f)
	, OcclusionCheckInterval(0.0f)
	, LastOcclusionCheckTime(TNumericLimits<float>::Lowest())
	, MaxDistance(WORLD_MAX)
	, LastLocation(FVector::ZeroVector)
	, AudioVolumeID(0)
	, LastUpdateTime(0.0f)
	, SourceInteriorVolume(1.0f)
	, SourceInteriorLPF(MAX_FILTER_FREQUENCY)
	, CurrentInteriorVolume(1.0f)
	, CurrentInteriorLPF(MAX_FILTER_FREQUENCY)
	, EnvelopeFollowerAttackTime(10)
	, EnvelopeFollowerReleaseTime(100)
#if ENABLE_AUDIO_DEBUG
	, DebugColor(FColor::Black)
#endif // ENABLE_AUDIO_DEBUG
{
	if (!ActiveSoundTraceDelegate.IsBound())
	{
		ActiveSoundTraceDelegate.BindStatic(&OcclusionTraceDone);
	}
}

FActiveSound::~FActiveSound()
{
	ensureMsgf(WaveInstances.Num() == 0, TEXT("Destroyed an active sound that had active wave instances."));
	check(CanDelete());
}

FActiveSound* FActiveSound::CreateVirtualCopy(const FActiveSound& InActiveSoundToCopy, FAudioDevice& InAudioDevice)
{
	check(!InActiveSoundToCopy.bIsStopping);

	FActiveSound* ActiveSound = new FActiveSound(InActiveSoundToCopy);

	ActiveSound->bAsyncOcclusionPending = false;
	ActiveSound->bHasVirtualized = true;
	ActiveSound->bIsPlayingAudio = false;
	ActiveSound->bShouldStopDueToMaxConcurrency = false;
	ActiveSound->AudioDevice = &InAudioDevice;
	ActiveSound->PlaybackTimeNonVirtualized = 0.0f;

	// If volume concurrency tracking is enabled, reset the value,
	// otherwise keep disabled
	if (InActiveSoundToCopy.VolumeConcurrency >= 0.0f)
	{
		ActiveSound->VolumeConcurrency = 1.0f;
	}

	ActiveSound->ConcurrencyGroupData.Reset();
	ActiveSound->WaveInstances.Reset();

	return ActiveSound;
}

FArchive& operator<<( FArchive& Ar, FActiveSound* ActiveSound )
{
	if( !Ar.IsLoading() && !Ar.IsSaving() )
	{
		Ar << ActiveSound->Sound;
		Ar << ActiveSound->WaveInstances;
		Ar << ActiveSound->SoundNodeOffsetMap;
	}
	return( Ar );
}

void FActiveSound::AddReferencedObjects( FReferenceCollector& Collector)
{
	for (auto WaveInstanceIt(WaveInstances.CreateConstIterator()); WaveInstanceIt; ++WaveInstanceIt)
	{
		FWaveInstance* WaveInstance = WaveInstanceIt.Value();
		// Avoid recursing back to the wave instance that sourced this active sound
		if( WaveInstance )
		{
			WaveInstance->AddReferencedObjects( Collector );
		}
	}

	Collector.AddReferencedObject(SoundClassOverride);

	Collector.AddReferencedObject(SourceEffectChain);
	if (SourceEffectChain)
	{
		SourceEffectChain->AddReferencedEffects(Collector);
	}

	Collector.AddReferencedObject(Sound);
	if (Sound && Sound->SourceEffectChain)
	{
		Sound->SourceEffectChain->AddReferencedEffects(Collector);
	}

	for (USoundConcurrency* Concurrency : ConcurrencySet)
	{
		if (Concurrency)
		{
			Collector.AddReferencedObject(Concurrency);
		}
	}

	for (FAudioComponentParam& Param : InstanceParameters)
	{
		if (Param.SoundWaveParam)
		{
			Collector.AddReferencedObject(Param.SoundWaveParam);
		}
	}
}

int32 FActiveSound::GetPlayCount() const
{
	if (!Sound || !AudioDevice)
	{
		return 0;
	}

	if (const int32* PlayCount = Sound->CurrentPlayCount.Find(AudioDevice->DeviceHandle))
	{
		return *PlayCount;
	}

	return 0;
}

void FActiveSound::SetPitch(float Value)
{
	PitchMultiplier = Value;
}

void FActiveSound::SetVolume(float Value)
{
	VolumeMultiplier = Value;
}

void FActiveSound::SetWorld(UWorld* InWorld)
{
	World = InWorld;
	WorldID = (InWorld ? InWorld->GetUniqueID() : 0);
}

void FActiveSound::SetSound(USoundBase* InSound)
{
	Sound = InSound;
	bApplyInteriorVolumes = (SoundClassOverride && SoundClassOverride->Properties.bApplyAmbientVolumes)
							|| (Sound && Sound->ShouldApplyInteriorVolumes());
}

void FActiveSound::SetSourceEffectChain(USoundEffectSourcePresetChain* InSourceEffectChain)
{
	SourceEffectChain = InSourceEffectChain;
}

void FActiveSound::SetSoundClass(USoundClass* SoundClass)
{
	SoundClassOverride = SoundClass;
	bApplyInteriorVolumes = (SoundClassOverride && SoundClassOverride->Properties.bApplyAmbientVolumes)
							|| (Sound && Sound->ShouldApplyInteriorVolumes());
}

bool FActiveSound::IsPlayWhenSilent() const
{
	if (!AudioDevice || !AudioDevice->PlayWhenSilentEnabled())
	{
		return false;
	}

	return Sound && Sound->IsPlayWhenSilent();
}

void FActiveSound::ClearAudioComponent()
{
	AudioComponentID = 0;
	AudioComponentUserID = NAME_None;
	AudioComponentName = NAME_None;

	OwnerID = 0;
	OwnerName = NAME_None;
}

void FActiveSound::SetAudioComponent(const FActiveSound& ActiveSound)
{
	AudioComponentID = ActiveSound.AudioComponentID;
	AudioComponentUserID = ActiveSound.AudioComponentUserID;
	AudioComponentName = ActiveSound.AudioComponentName;

	OwnerID = ActiveSound.OwnerID;
	OwnerName = ActiveSound.OwnerName;
}

void FActiveSound::SetAudioComponent(const UAudioComponent& Component)
{
	check(IsInGameThread());

	AActor* Owner = Component.GetOwner();

	AudioComponentID = Component.GetAudioComponentID();
	AudioComponentUserID = Component.GetAudioComponentUserID();
	AudioComponentName = Component.GetFName();

	SetOwner(Owner);
}

void FActiveSound::SetOwner(AActor* Actor)
{
	if (Actor)
	{
		OwnerID = Actor->GetUniqueID();
		OwnerName = Actor->GetFName();
	}
	else
	{
		OwnerID = 0;
		OwnerName = NAME_None;
	}
}

FString FActiveSound::GetAudioComponentName() const
{
	return (AudioComponentID > 0 ? AudioComponentName.ToString() : TEXT("NO COMPONENT"));
}

FString FActiveSound::GetOwnerName() const
{
	return (OwnerID > 0 ? OwnerName.ToString() : TEXT("None"));
}

USoundClass* FActiveSound::GetSoundClass() const
{
	if (SoundClassOverride)
	{
		return SoundClassOverride;
	}
	else if (Sound)
	{
		return Sound->GetSoundClass();
	}

	return nullptr;
}

USoundSubmix* FActiveSound::GetSoundSubmix() const
{
	return Sound ? Sound->GetSoundSubmix() : nullptr;
}

void FActiveSound::SetSubmixSend(const FSoundSubmixSendInfo& SubmixSendInfo)
{
	// Override send level if the submix send already included in active sound
	for (FSoundSubmixSendInfo& Info : SoundSubmixSendsOverride)
	{
		if (Info.SoundSubmix == SubmixSendInfo.SoundSubmix)
		{
			Info.SendLevel = SubmixSendInfo.SendLevel;
			return;
		}
	}

	// Otherwise, add it to the submix send overrides
	SoundSubmixSendsOverride.Add(SubmixSendInfo);
}

void FActiveSound::SetSourceBusSend(EBusSendType BusSendType, const FSoundSourceBusSendInfo& SourceBusSendInfo)
{
	// Override send level if the source bus send is already included in active sound
	for (FSoundSourceBusSendInfo& Info : SoundSourceBusSendsOverride[(int32)BusSendType])
	{
		if (Info.SoundSourceBus == SourceBusSendInfo.SoundSourceBus)
		{
			Info.SendLevel = SourceBusSendInfo.SendLevel;
			return;
		}
	}

	// Otherwise, add it to the source bus send overrides
	SoundSourceBusSendsOverride[(int32)BusSendType].Add(SourceBusSendInfo);
}

void FActiveSound::Stop()
{
	if (AudioDevice)
	{
		AudioDevice->AddSoundToStop(this);
	}
}

void FActiveSound::GetSoundSubmixSends(TArray<FSoundSubmixSendInfo>& OutSends) const
{
	if (Sound)
	{
		// Get the base sends
		Sound->GetSoundSubmixSends(OutSends);

		// Loop through the overrides, which may append or override the existing send
		for (const FSoundSubmixSendInfo& SendInfo : SoundSubmixSendsOverride)
		{
			bool bOverridden = false;
			for (FSoundSubmixSendInfo& OutSendInfo : OutSends)
			{
				if (OutSendInfo.SoundSubmix == SendInfo.SoundSubmix)
				{
					OutSendInfo.SendLevel = SendInfo.SendLevel;
					bOverridden = true;
					break;
				}
			}

			if (!bOverridden)
			{
				OutSends.Add(SendInfo);
			}
		}
	}
}

void FActiveSound::GetSoundSourceBusSends(EBusSendType BusSendType, TArray<FSoundSourceBusSendInfo>& OutSends) const
{
	if (Sound)
	{
		// Get the base sends
		Sound->GetSoundSourceBusSends(BusSendType, OutSends);

		// Loop through the overrides, which may append or override the existing send
		for (const FSoundSourceBusSendInfo& SendInfo : SoundSourceBusSendsOverride[(int32)BusSendType])
		{
			bool bOverridden = false;
			for (FSoundSourceBusSendInfo& OutSendInfo : OutSends)
			{
				if (OutSendInfo.SoundSourceBus == SendInfo.SoundSourceBus)
				{
					OutSendInfo.SendLevel = SendInfo.SendLevel;
					bOverridden = true;
					break;
				}
			}

			if (!bOverridden)
			{
				OutSends.Add(SendInfo);
			}
		}
	}
}

int32 FActiveSound::FindClosestListener(const TArray<struct FListener>& InListeners) const
{
	return AudioDevice ? AudioDevice->FindClosestListenerIndex(Transform, InListeners) : INDEX_NONE;
}

int32 FActiveSound::FindClosestListener() const
{
	return AudioDevice ? AudioDevice->FindClosestListenerIndex(Transform) : INDEX_NONE;
}

void FActiveSound::GetConcurrencyHandles(TArray<FConcurrencyHandle>& OutConcurrencyHandles) const
{
	OutConcurrencyHandles.Reset();

	if (!ConcurrencySet.Num() && Sound)
	{
		Sound->GetConcurrencyHandles(OutConcurrencyHandles);
	}
	else
	{
		for (const USoundConcurrency* Concurrency : ConcurrencySet)
		{
			if (Concurrency)
			{
				OutConcurrencyHandles.Emplace(FConcurrencyHandle(*Concurrency));
			}
		}
	}
}

bool FActiveSound::GetConcurrencyFadeDuration(float& OutFadeDuration) const
{
	OutFadeDuration = -1.0f;
	TArray<FConcurrencyHandle> Handles;
	GetConcurrencyHandles(Handles);
	for (FConcurrencyHandle& Handle : Handles)
	{
		// Resolution rules that don't support eviction (effectively requiring a sound to start before culling)
		// can spam if a looping ActiveSound isn't active longer than a virtualization update period, which
		// can happen when a concurrency group is maxed and constantly evicting.  If the voice steal fade time is particularly
		// long, this can flood the active sound count. Therefore, only use the voice steal fade time if the sound has been
		// active for a sufficient period of time.
		if (!Handle.Settings.IsEvictionSupported() && IsLooping() && FMath::IsNearlyZero(PlaybackTimeNonVirtualized, 0.1f))
		{
			OutFadeDuration = 0.0f;
			return false;
		}

		OutFadeDuration = OutFadeDuration < 0.0f
			? Handle.Settings.VoiceStealReleaseTime
			: FMath::Min(Handle.Settings.VoiceStealReleaseTime, OutFadeDuration);
	}

	// Negative if no handles are found, so return no fade required.
	if (OutFadeDuration <= 0.0f)
	{
		OutFadeDuration = 0.0f;
		return false;
	}

	return true;
}

void FActiveSound::UpdateWaveInstances(TArray<FWaveInstance*> &InWaveInstances, const float DeltaTime)
{
	check(AudioDevice);

	// Reset whether or not the active sound is playing audio.
	bIsPlayingAudio = false;

	// Reset the active sound's min current pitch value. This is updated as sounds try to play and determine their pitch values.
	MinCurrentPitch = 1.0f;

	// Early outs.
	if (Sound == nullptr || !Sound->IsPlayable())
	{
		return;
	}

	{
		SCOPE_CYCLE_COUNTER( STAT_AudioFindNearestLocation );
		ClosestListenerIndex = AudioDevice->FindClosestListenerIndex(Transform);
	}

	FocusData.PriorityHighest = 1.0f;

	FSoundParseParameters ParseParams;
	ParseParams.Transform = Transform;
	ParseParams.StartTime = RequestedStartTime;

	ComponentVolumeFader.Update(DeltaTime);

	ParseParams.VolumeMultiplier = GetVolume();

	ParseParams.Priority = Priority;
	ParseParams.Pitch *= GetPitch() * Sound->GetPitchMultiplier();
	ParseParams.bEnableLowPassFilter = bEnableLowPassFilter;
	ParseParams.LowPassFilterFrequency = LowPassFilterFrequency;
	ParseParams.SoundClass = GetSoundClass();
	ParseParams.bIsPaused = bIsPaused;

	ParseParams.SoundSubmix = GetSoundSubmix();
	GetSoundSubmixSends(ParseParams.SoundSubmixSends);

	ParseParams.bOutputToBusOnly = Sound->bOutputToBusOnly;

	for (int32 BusSendType = 0; BusSendType < (int32)EBusSendType::Count; ++BusSendType)
	{
		GetSoundSourceBusSends((EBusSendType)BusSendType, ParseParams.SoundSourceBusSends[BusSendType]);
	}

	// Set up the base source effect chain.
	ParseParams.SourceEffectChain = GetSourceEffectChain();

	// Setup the envelope attack and release times
	ParseParams.EnvelopeFollowerAttackTime = EnvelopeFollowerAttackTime;
	ParseParams.EnvelopeFollowerReleaseTime = EnvelopeFollowerReleaseTime;

	if (bApplyInteriorVolumes)
	{
		// Additional inside/outside processing for ambient sounds
		// If we aren't in a world there is no interior volumes to be handled.
		HandleInteriorVolumes(ParseParams);
	}

	// for velocity-based effects like doppler
	if (DeltaTime > 0.f)
	{
		ParseParams.Velocity = (ParseParams.Transform.GetTranslation() - LastLocation) / DeltaTime;
		LastLocation = ParseParams.Transform.GetTranslation();
	}

	TArray<FWaveInstance*> ThisSoundsWaveInstances;

	// Recurse nodes, have SoundWave's create new wave instances and update bFinished unless we finished fading out.
	bFinished = true;
	if (FadeOut == EFadeOut::None || ComponentVolumeFader.IsActive())
	{
		bool bReverbSendLevelWasSet = false;
		if (bHasAttenuationSettings)
		{
			UpdateAttenuation(DeltaTime, ParseParams, ClosestListenerIndex);
			bReverbSendLevelWasSet = true;
		}
		else
		{
			ParseParams.ReverbSendMethod = EReverbSendMethod::Manual;
			if (ParseParams.SoundClass)
			{
				ParseParams.ManualReverbSendLevel = ParseParams.SoundClass->Properties.Default2DReverbSendAmount;
			}
			else
			{
				ParseParams.ManualReverbSendLevel = AudioDevice->GetDefaultReverbSendLevel();
			}
		}

		ParseParams.ModulationPluginSettings = FindModulationSettings();
		Sound->Parse(AudioDevice, 0, *this, ParseParams, ThisSoundsWaveInstances);

		// Track this active sound's min pitch value. This is used to scale it's possible duration value.
		if (ParseParams.Pitch < MinCurrentPitch)
		{
			MinCurrentPitch = ParseParams.Pitch;
		}
	}

	if (bFinished)
	{
		AudioDevice->AddSoundToStop(this);
	}
	else if (ThisSoundsWaveInstances.Num() > 0)
	{
		// Let the wave instance know that this active sound is stopping. This will result in the wave instance getting a lower sort for voice prioritization
		if (IsStopping())
		{
			for (FWaveInstance* WaveInstance : ThisSoundsWaveInstances)
			{
				WaveInstance->SetStopping(true);
			}
		}

		// If the concurrency volume is negative (as set by ConcurrencyManager on creation),
		// skip updating as its been deemed unnecessary
		if (VolumeConcurrency >= 0.0f)
		{
			// Now that we have this sound's active wave instances, lets find the loudest active wave instance to represent the "volume" of this active sound
			VolumeConcurrency = 0.0f;
			for (const FWaveInstance* WaveInstance : ThisSoundsWaveInstances)
			{
				check(WaveInstance);

				float WaveInstanceVolume = WaveInstance->GetVolumeWithDistanceAttenuation() * WaveInstance->GetDynamicVolume();
				if (WaveInstanceVolume > VolumeConcurrency)
				{
					VolumeConcurrency = WaveInstanceVolume;
				}
			}

			// Remove concurrency volume scalars as this can cause ping-ponging to occur with virtualization and loops
			// utilizing concurrency with rules that don't support eviction (removal from concurrency system prior to playback).
			const float VolumeScale = GetTotalConcurrencyVolumeScale();
			if (VolumeScale > SMALL_NUMBER)
			{
				VolumeConcurrency /= VolumeScale;
			}
			else
			{
				VolumeConcurrency = 0.0f;
			}
		}

		// Check to see if we need to broadcast the envelope value of sounds playing with this active sound
		if (AudioComponentID > 0)
		{
			if (bUpdateMultiEnvelopeValue)
			{
				int32 NumWaveInstances = ThisSoundsWaveInstances.Num();

				// Add up the envelope value for every wave instance so we get a sum of the envelope value for all sources.
				float EnvelopeValueSum = 0.0f;
				float MaxEnvelopeValue = 0.0f;
				for (FWaveInstance* WaveInstance : ThisSoundsWaveInstances)
				{
					const float WaveInstanceEnvelopeValue = WaveInstance->GetEnvelopeValue();
					EnvelopeValueSum += WaveInstanceEnvelopeValue;
					MaxEnvelopeValue = FMath::Max(WaveInstanceEnvelopeValue, MaxEnvelopeValue);
				}

				// Now divide by the number of instances to get the average
				float AverageEnvelopeValue = EnvelopeValueSum / NumWaveInstances;
				uint64 AudioComponentIDCopy = AudioComponentID;
				FAudioThread::RunCommandOnGameThread([AudioComponentIDCopy, AverageEnvelopeValue, MaxEnvelopeValue, NumWaveInstances]()
				{
					if (UAudioComponent* AudioComponent = UAudioComponent::GetAudioComponentFromID(AudioComponentIDCopy))
					{
						if (AudioComponent->OnAudioMultiEnvelopeValue.IsBound())
						{
							AudioComponent->OnAudioMultiEnvelopeValue.Broadcast(AverageEnvelopeValue, MaxEnvelopeValue, NumWaveInstances);
						}

						if (AudioComponent->OnAudioMultiEnvelopeValueNative.IsBound())
						{
							AudioComponent->OnAudioMultiEnvelopeValueNative.Broadcast(AudioComponent, AverageEnvelopeValue, MaxEnvelopeValue, NumWaveInstances);
						}
					}
				});
			}

			if (bUpdatePlaybackTime)
			{
				TMap<uint32, float> WaveInstancePlaybackTimes;

				// Update each of the wave instances playback time based on delta time and the wave instances pitch value
				for (FWaveInstance* WaveInstance : ThisSoundsWaveInstances)
				{
					WaveInstance->PlaybackTime += DeltaTime * WaveInstance->Pitch;

					// For looping sounds, we need to check the wrapping condition
					if (WaveInstance->LoopingMode != LOOP_Never)
					{
						float Duration = WaveInstance->WaveData->Duration;
						if (WaveInstance->PlaybackTime > Duration)
						{
							WaveInstance->PlaybackTime = 0.0f;
						}
					}
					WaveInstancePlaybackTimes.Add(WaveInstance->WaveData->GetUniqueID(), WaveInstance->PlaybackTime);
				}
				uint64 AudioComponentIDCopy = AudioComponentID;
				FAudioThread::RunCommandOnGameThread([AudioComponentIDCopy, WaveInstancePlaybackTimes]()
				{
					if (UAudioComponent* AudioComponent = UAudioComponent::GetAudioComponentFromID(AudioComponentIDCopy))
					{
						AudioComponent->SetPlaybackTimes(WaveInstancePlaybackTimes);
					}
				});
			}
		}

	}

#if ENABLE_AUDIO_DEBUG
	if (DebugColor == FColor::Black)
	{
		DebugColor = FColor::MakeRandomColor();
	}
	FAudioDebugger::DrawDebugInfo(*this, ThisSoundsWaveInstances, DeltaTime);
#endif // ENABLE_AUDIO_DEBUG

	InWaveInstances.Append(ThisSoundsWaveInstances);
}

USoundModulationPluginSourceSettingsBase* FActiveSound::FindModulationSettings() const
{
	if (UClass* PluginClass = GetAudioPluginCustomSettingsClass(EAudioPlugin::MODULATION))
	{
		for (USoundModulationPluginSourceSettingsBase* Settings : Sound->Modulation.Settings)
		{
			if (Settings && Settings->IsA(PluginClass))
			{
				return Settings;
			}
		}

		if (UAudioComponent* AudioComponent = UAudioComponent::GetAudioComponentFromID(AudioComponentID))
		{
			for (USoundModulationPluginSourceSettingsBase* Settings : AudioComponent->Modulation.Settings)
			{
				if (Settings && Settings->IsA(PluginClass))
				{
					return Settings;
				}
			}
		}

		if (USoundClass* SoundClass = Sound->GetSoundClass())
		{
			for (USoundModulationPluginSourceSettingsBase* Settings : SoundClass->Modulation.Settings)
			{
				if (Settings && Settings->IsA(PluginClass))
				{
					return Settings;
				}
			}
		}
	}

	return nullptr;
}

void FActiveSound::MarkPendingDestroy(bool bDestroyNow)
{
	check(AudioDevice);

	bool bWasStopping = bIsStopping;

	if (Sound && !bIsStopping)
	{
		int32* PlayCount = AudioDevice ? Sound->CurrentPlayCount.Find(AudioDevice->DeviceHandle) : nullptr;
		if (PlayCount)
		{
			*PlayCount = FMath::Max(*PlayCount - 1, 0);
			if (*PlayCount == 0)
			{
				Sound->CurrentPlayCount.Remove(AudioDevice->DeviceHandle);
			}
		}

		if (AudioDevice->IsModulationPluginEnabled() && AudioDevice->ModulationInterface.IsValid())
		{
			if (USoundModulationPluginSourceSettingsBase* ModulationSettings = FindModulationSettings())
			{
				AudioDevice->ModulationInterface->OnReleaseSound(static_cast<ISoundModulatable&>(*this));
			}
		}
	}

	TArray<FWaveInstance*> ToDelete;
	for (TPair<UPTRINT, FWaveInstance*> WaveInstanceIt : WaveInstances)
	{
		FWaveInstance* WaveInstance = WaveInstanceIt.Value;

		// Stop the owning sound source
		FSoundSource* Source = AudioDevice->GetSoundSource(WaveInstance);
		if (Source)
		{
			bool bStopped = false;
			if (AudioDevice->IsAudioMixerEnabled() && AudioDevice->IsStoppingVoicesEnabled())
			{
				if (bDestroyNow || !AudioDevice->GetNumFreeSources())
				{
					Source->StopNow();
					bStopped = true;
				}
			}

			if (!bStopped)
			{
				Source->Stop();
			}
		}

		if (!bIsStopping)
		{
			// Dequeue subtitles for this sounds on the game thread
			DECLARE_CYCLE_STAT(TEXT("FGameThreadAudioTask.KillSubtitles"), STAT_AudioKillSubtitles, STATGROUP_TaskGraphTasks);
			const PTRINT WaveInstanceID = (PTRINT)WaveInstance;
			FAudioThread::RunCommandOnGameThread([WaveInstanceID]()
			{
				FSubtitleManager::GetSubtitleManager()->KillSubtitles(WaveInstanceID);
			}, GET_STATID(STAT_AudioKillSubtitles));
		}

		if (Source)
		{
			if (!Source->IsStopping())
			{
				Source->StopNow();

				ToDelete.Add(WaveInstance);
			}
			else
			{
				// This source is doing a fade out, so stopping. Can't remove the wave instance yet.
				bIsStopping = true;
			}
		}
		else
		{
			// Have a wave instance but no source.
			ToDelete.Add(WaveInstance);
		}
	}

	for (FWaveInstance* WaveInstance : ToDelete)
	{
		RemoveWaveInstance(WaveInstance->WaveInstanceHash);
	}

	if (bDestroyNow)
	{
		bIsStopping = false;
	}

	if (!bWasStopping)
	{
		AudioDevice->RemoveActiveSound(this);
	}
}

bool FActiveSound::UpdateStoppingSources(uint64 CurrentTick, bool bEnsureStopped)
{
	// If we're not stopping, then just return true (we can be cleaned up)
	if (!bIsStopping)
	{
		return true;
	}

	bIsStopping = false;

	TArray<FWaveInstance*> ToDelete;
	for (TPair<UPTRINT, FWaveInstance*> WaveInstanceIt : WaveInstances)
	{
		FWaveInstance* WaveInstance = WaveInstanceIt.Value;

		// Some wave instances in the list here may be nullptr if some sounds have already stopped or didn't need to do a stop
		if (WaveInstance)
		{
			// Stop the owning sound source
			FSoundSource* Source = AudioDevice->GetSoundSource(WaveInstance);
			if (Source)
			{
				// The source has finished (totally faded out)
				if (Source->IsFinished() || bEnsureStopped)
				{
					Source->StopNow();

					// Delete the wave instance
					ToDelete.Add(WaveInstance);
				}
				else
				{
					// We are not finished yet so touch it
					Source->LastUpdate = CurrentTick;
					Source->LastHeardUpdate = CurrentTick;

					// flag that we're still stopping (return value)
					bIsStopping = true;
				}
			}
			else
			{
				// We have a wave instance but no source for it, so just delete it.
				ToDelete.Add(WaveInstance);
			}
		}
	}

	for (FWaveInstance* WaveInstance : ToDelete)
	{
		RemoveWaveInstance(WaveInstance->WaveInstanceHash);
	}

	// Return true to indicate this active sound can be cleaned up
	// If we've reached this point, all sound waves have stopped so we can clear this wave instance out.
	if (!bIsStopping)
	{
		check(WaveInstances.Num() == 0);
		return true;
	}

	// still stopping!
	return false;
}

FWaveInstance* FActiveSound::FindWaveInstance(const UPTRINT WaveInstanceHash)
{
	return WaveInstances.FindRef(WaveInstanceHash);
}

void FActiveSound::RemoveWaveInstance(const UPTRINT WaveInstanceHash)
{
	if (FWaveInstance* WaveInstance = WaveInstances.FindRef(WaveInstanceHash))
	{
		WaveInstances.Remove(WaveInstanceHash);
		delete WaveInstance;
	}
}

void FActiveSound::OcclusionTraceDone(const FTraceHandle& TraceHandle, FTraceDatum& TraceDatum)
{
	// Look for any results that resulted in a blocking hit
	bool bFoundBlockingHit = false;
	for (const FHitResult& HitResult : TraceDatum.OutHits)
	{
		if (HitResult.bBlockingHit)
		{
			bFoundBlockingHit = true;
			break;
		}
	}

	FAsyncTraceDetails TraceDetails;
	if (TraceToActiveSoundMap.RemoveAndCopyValue(TraceHandle, TraceDetails))
	{
		if (FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager())
		{
			if (FAudioDevice* AudioDevice = AudioDeviceManager->GetAudioDevice(TraceDetails.AudioDeviceID))
			{
				FActiveSound* ActiveSound = TraceDetails.ActiveSound;

				DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.OcclusionTraceDone"), STAT_OcclusionTraceDone, STATGROUP_AudioThreadCommands);

				FAudioThread::RunCommandOnAudioThread([AudioDevice, ActiveSound, bFoundBlockingHit]()
				{
					AudioDevice->NotifyActiveSoundOcclusionTraceDone(ActiveSound, bFoundBlockingHit);
				}, GET_STATID(STAT_OcclusionTraceDone));
			}
		}
	}
}

void FActiveSound::CheckOcclusion(const FVector ListenerLocation, const FVector SoundLocation, const FSoundAttenuationSettings* AttenuationSettingsPtr)
{
	check(AttenuationSettingsPtr);
	check(AttenuationSettingsPtr->bEnableOcclusion);

	float InterpolationTime = 0.0f;

	// If occlusion is disabled by cvar, we're always going to be not occluded
	if (AudioOcclusionDisabledCvar == 1)
	{
		bIsOccluded = false;
	}
	else
	{
		if (!bAsyncOcclusionPending && (PlaybackTime - LastOcclusionCheckTime) > OcclusionCheckInterval)
		{
			LastOcclusionCheckTime = PlaybackTime;

			const bool bUseComplexCollisionForOcclusion = AttenuationSettingsPtr->bUseComplexCollisionForOcclusion;
			const ECollisionChannel OcclusionTraceChannel = AttenuationSettingsPtr->OcclusionTraceChannel;

			if (!bHasCheckedOcclusion)
			{
				FCollisionQueryParams Params(SCENE_QUERY_STAT(SoundOcclusion), bUseComplexCollisionForOcclusion);
				if (OwnerID > 0)
				{
					Params.AddIgnoredActor(OwnerID);
				}

				if (UWorld* WorldPtr = World.Get())
				{
					// LineTraceTestByChannel is generally threadsafe, but there is a very narrow race condition here
					// if World goes invalid before the scene lock and queries begin.
					bIsOccluded = WorldPtr->LineTraceTestByChannel(SoundLocation, ListenerLocation, OcclusionTraceChannel, Params);
				}
			}
			else
			{
				bAsyncOcclusionPending = true;

				const uint32 SoundOwnerID = OwnerID;
				TWeakObjectPtr<UWorld> SoundWorld = World;
				FAsyncTraceDetails TraceDetails;
				TraceDetails.AudioDeviceID = AudioDevice->DeviceHandle;
				TraceDetails.ActiveSound = this;

				FAudioThread::RunCommandOnGameThread([SoundWorld, SoundLocation, ListenerLocation, OcclusionTraceChannel, SoundOwnerID, bUseComplexCollisionForOcclusion, TraceDetails]
				{
					if (UWorld* WorldPtr = SoundWorld.Get())
					{
						FCollisionQueryParams Params(SCENE_QUERY_STAT(SoundOcclusion), bUseComplexCollisionForOcclusion);
						if (SoundOwnerID > 0)
						{
							Params.AddIgnoredActor(SoundOwnerID);
						}

						FTraceHandle TraceHandle = WorldPtr->AsyncLineTraceByChannel(EAsyncTraceType::Test, SoundLocation, ListenerLocation, OcclusionTraceChannel, Params, FCollisionResponseParams::DefaultResponseParam, &ActiveSoundTraceDelegate);
						TraceToActiveSoundMap.Add(TraceHandle, TraceDetails);
					}
				});
			}
		}

		// Update the occlusion values
		if (bHasCheckedOcclusion)
		{
			InterpolationTime = AttenuationSettingsPtr->OcclusionInterpolationTime;
		}
		bHasCheckedOcclusion = true;
	}

	if (bIsOccluded)
	{
		if (CurrentOcclusionFilterFrequency.GetTargetValue() > AttenuationSettingsPtr->OcclusionLowPassFilterFrequency)
		{
			CurrentOcclusionFilterFrequency.Set(AttenuationSettingsPtr->OcclusionLowPassFilterFrequency, InterpolationTime);
		}

		if (CurrentOcclusionVolumeAttenuation.GetTargetValue() > AttenuationSettingsPtr->OcclusionVolumeAttenuation)
		{
			CurrentOcclusionVolumeAttenuation.Set(AttenuationSettingsPtr->OcclusionVolumeAttenuation, InterpolationTime);
		}
	}
	else
	{
		CurrentOcclusionFilterFrequency.Set(MAX_FILTER_FREQUENCY, InterpolationTime);
		CurrentOcclusionVolumeAttenuation.Set(1.0f, InterpolationTime);
	}

	const float DeltaTime = FApp::GetDeltaTime();
	CurrentOcclusionFilterFrequency.Update(DeltaTime);
	CurrentOcclusionVolumeAttenuation.Update(DeltaTime);
}

void FActiveSound::HandleInteriorVolumes(FSoundParseParameters& ParseParams)
{
	// Get the settings of the ambient sound
	if (!bGotInteriorSettings || (ParseParams.Transform.GetTranslation() - LastLocation).SizeSquared() > KINDA_SMALL_NUMBER)
	{
		FAudioDevice::FAudioVolumeSettings AudioVolumeSettings;
		AudioDevice->GetAudioVolumeSettings(WorldID, ParseParams.Transform.GetTranslation(), AudioVolumeSettings);

		InteriorSettings = AudioVolumeSettings.InteriorSettings;
		AudioVolumeID = AudioVolumeSettings.AudioVolumeID;
		bGotInteriorSettings = true;
	}

	check(IsInAudioThread());
	const TArray<FListener>& Listeners = AudioDevice->GetListeners();
	check(ClosestListenerIndex < Listeners.Num());
	const FListener& Listener = Listeners[ClosestListenerIndex];

	// Check to see if we've moved to a new audio volume
	if (LastUpdateTime < Listener.InteriorStartTime)
	{
		SourceInteriorVolume = CurrentInteriorVolume;
		SourceInteriorLPF = CurrentInteriorLPF;
		LastUpdateTime = FApp::GetCurrentTime();
	}

	if (Listener.AudioVolumeID == AudioVolumeID || !bAllowSpatialization)
	{
		// Ambient and listener in same ambient zone
		CurrentInteriorVolume = FMath::Lerp(SourceInteriorVolume, 1.0f, Listener.InteriorVolumeInterp);
		ParseParams.InteriorVolumeMultiplier = CurrentInteriorVolume;

		CurrentInteriorLPF = FMath::Lerp(SourceInteriorLPF, MAX_FILTER_FREQUENCY, Listener.InteriorLPFInterp);
		ParseParams.AmbientZoneFilterFrequency = CurrentInteriorLPF;
	}
	else
	{
		// Ambient and listener in different ambient zone
		if( InteriorSettings.bIsWorldSettings )
		{
			// The ambient sound is 'outside' - use the listener's exterior volume
			CurrentInteriorVolume = FMath::Lerp(SourceInteriorVolume, Listener.InteriorSettings.ExteriorVolume, Listener.ExteriorVolumeInterp);
			ParseParams.InteriorVolumeMultiplier = CurrentInteriorVolume;

			CurrentInteriorLPF = FMath::Lerp(SourceInteriorLPF, Listener.InteriorSettings.ExteriorLPF, Listener.ExteriorLPFInterp);
			ParseParams.AmbientZoneFilterFrequency = CurrentInteriorLPF;
		}
		else
		{
			// The ambient sound is 'inside' - use the ambient sound's interior volume multiplied with the listeners exterior volume
			CurrentInteriorVolume = FMath::Lerp(SourceInteriorVolume, InteriorSettings.InteriorVolume, Listener.InteriorVolumeInterp);
			CurrentInteriorVolume *= FMath::Lerp(SourceInteriorVolume, Listener.InteriorSettings.ExteriorVolume, Listener.ExteriorVolumeInterp);
			ParseParams.InteriorVolumeMultiplier = CurrentInteriorVolume;

			float AmbientLPFValue = FMath::Lerp(SourceInteriorLPF, InteriorSettings.InteriorLPF, Listener.InteriorLPFInterp);
			float ListenerLPFValue = FMath::Lerp(SourceInteriorLPF, Listener.InteriorSettings.ExteriorLPF, Listener.ExteriorLPFInterp);

			// The current interior LPF value is the less of the LPF due to ambient zone and LPF due to listener settings
			if (AmbientLPFValue < ListenerLPFValue)
			{
				CurrentInteriorLPF = AmbientLPFValue;
				ParseParams.AmbientZoneFilterFrequency = AmbientLPFValue;
			}
			else
			{
				CurrentInteriorLPF = ListenerLPFValue;
				ParseParams.AmbientZoneFilterFrequency = ListenerLPFValue;
			}
		}
	}
}

FWaveInstance& FActiveSound::AddWaveInstance(const UPTRINT WaveInstanceHash)
{
	FWaveInstance* WaveInstance = new FWaveInstance(WaveInstanceHash, *this);
	WaveInstances.Add(WaveInstanceHash, WaveInstance);
	return *WaveInstance;
}

void FActiveSound::ApplyRadioFilter(const FSoundParseParameters& ParseParams )
{
	check(AudioDevice);
	if( AudioDevice->GetMixDebugState() != DEBUGSTATE_DisableRadio )
	{
		// Make sure the radio filter is requested
		if( ParseParams.SoundClass)
		{
			const float RadioFilterVolumeThreshold = ParseParams.VolumeMultiplier * ParseParams.SoundClass->Properties.RadioFilterVolumeThreshold;
			if (RadioFilterVolumeThreshold > KINDA_SMALL_NUMBER )
			{
				bApplyRadioFilter = ( ParseParams.Volume < RadioFilterVolumeThreshold );
			}
		}
	}
	else
	{
		bApplyRadioFilter = false;
	}

	bRadioFilterSelected = true;
}

bool FActiveSound::GetFloatParameter( const FName InName, float& OutFloat ) const
{
	// Always fail if we pass in no name.
	if( InName != NAME_None )
	{
		for( const FAudioComponentParam& P : InstanceParameters )
		{
			if( P.ParamName == InName )
			{
				OutFloat = P.FloatParam;
				return true;
			}
		}
	}

	return false;
}

float FActiveSound::GetVolume() const
{
	const float Volume = VolumeMultiplier * ComponentVolumeFader.GetVolume() * GetTotalConcurrencyVolumeScale();
	return Sound ? Volume * Sound->GetVolumeMultiplier() : Volume;
}

float FActiveSound::GetTotalConcurrencyVolumeScale() const
{
	float OutVolume = 1.0f;

	for (const TPair<FConcurrencyGroupID, FConcurrencySoundData>& ConcurrencyPair : ConcurrencyGroupData)
	{
		OutVolume *= ConcurrencyPair.Value.GetVolume();
	}

	return OutVolume;
}

void FActiveSound::UpdateConcurrencyVolumeScalars(const float DeltaTime)
{
	for (TPair<FConcurrencyGroupID, FConcurrencySoundData>& ConcurrencyPair : ConcurrencyGroupData)
	{
		ConcurrencyPair.Value.Update(DeltaTime);
	}
}

void FActiveSound::SetFloatParameter( const FName InName, const float InFloat )
{
	if( InName != NAME_None )
	{
		// First see if an entry for this name already exists
		for( FAudioComponentParam& P : InstanceParameters )
		{
			if( P.ParamName == InName )
			{
				P.FloatParam = InFloat;
				return;
			}
		}

		// We didn't find one, so create a new one.
		const int32 NewParamIndex = InstanceParameters.AddDefaulted();
		InstanceParameters[ NewParamIndex ].ParamName = InName;
		InstanceParameters[ NewParamIndex ].FloatParam = InFloat;
	}
}

bool FActiveSound::GetWaveParameter( const FName InName, USoundWave*& OutWave ) const
{
	// Always fail if we pass in no name.
	if( InName != NAME_None )
	{
		for( const FAudioComponentParam& P : InstanceParameters )
		{
			if( P.ParamName == InName )
			{
				OutWave = P.SoundWaveParam;
				return true;
			}
		}
	}

	return false;
}

void FActiveSound::SetWaveParameter( const FName InName, USoundWave* InWave )
{
	if( InName != NAME_None )
	{
		// First see if an entry for this name already exists
		for( FAudioComponentParam& P : InstanceParameters )
		{
			if( P.ParamName == InName )
			{
				P.SoundWaveParam = InWave;
				return;
			}
		}

		// We didn't find one, so create a new one.
		const int32 NewParamIndex = InstanceParameters.AddDefaulted();
		InstanceParameters[ NewParamIndex ].ParamName = InName;
		InstanceParameters[ NewParamIndex ].SoundWaveParam = InWave;
	}
}

bool FActiveSound::GetBoolParameter( const FName InName, bool& OutBool ) const
{
	// Always fail if we pass in no name.
	if( InName != NAME_None )
	{
		for( const FAudioComponentParam& P : InstanceParameters )
		{
			if( P.ParamName == InName )
			{
				OutBool = P.BoolParam;
				return true;
			}
		}
	}

	return false;
}

void FActiveSound::SetBoolParameter( const FName InName, const bool InBool )
{
	if( InName != NAME_None )
	{
		// First see if an entry for this name already exists
		for( FAudioComponentParam& P : InstanceParameters )
		{
			if( P.ParamName == InName )
			{
				P.BoolParam = InBool;
				return;
			}
		}

		// We didn't find one, so create a new one.
		const int32 NewParamIndex = InstanceParameters.AddDefaulted();
		InstanceParameters[ NewParamIndex ].ParamName = InName;
		InstanceParameters[ NewParamIndex ].BoolParam = InBool;
	}
}

bool FActiveSound::GetIntParameter( const FName InName, int32& OutInt ) const
{
	// Always fail if we pass in no name.
	if( InName != NAME_None )
	{
		for( const FAudioComponentParam& P : InstanceParameters )
		{
			if( P.ParamName == InName )
			{
				OutInt = P.IntParam;
				return true;
			}
		}
	}

	return false;
}

void FActiveSound::SetIntParameter( const FName InName, const int32 InInt )
{
	if( InName != NAME_None )
	{
		// First see if an entry for this name already exists
		for( FAudioComponentParam& P : InstanceParameters )
		{
			if( P.ParamName == InName )
			{
				P.IntParam = InInt;
				return;
			}
		}

		// We didn't find one, so create a new one.
		const int32 NewParamIndex = InstanceParameters.AddDefaulted();
		InstanceParameters[ NewParamIndex ].ParamName = InName;
		InstanceParameters[ NewParamIndex ].IntParam = InInt;
	}
}

void FActiveSound::SetSoundParameter(const FAudioComponentParam& Param)
{
	if (Param.ParamName != NAME_None)
	{
		// First see if an entry for this name already exists
		for( FAudioComponentParam& P : InstanceParameters )
		{
			if (P.ParamName == Param.ParamName)
			{
				P = Param;
				return;
			}
		}

		// We didn't find one, so create a new one.
		const int32 NewParamIndex = InstanceParameters.Add(Param);
	}
}

void FActiveSound::CollectAttenuationShapesForVisualization(TMultiMap<EAttenuationShape::Type, FBaseAttenuationSettings::AttenuationShapeDetails>& ShapeDetailsMap) const
{
	bool bFoundAttenuationSettings = false;

	if (bHasAttenuationSettings)
	{
		AttenuationSettings.CollectAttenuationShapesForVisualization(ShapeDetailsMap);
	}

	// For sound cues we'll dig in and see if we can find any attenuation sound nodes that will affect the settings
	USoundCue* SoundCue = Cast<USoundCue>(Sound);
	if (SoundCue)
	{
		TArray<USoundNodeAttenuation*> AttenuationNodes;
		SoundCue->RecursiveFindAttenuation( SoundCue->FirstNode, AttenuationNodes );
		for (int32 NodeIndex = 0; NodeIndex < AttenuationNodes.Num(); ++NodeIndex)
		{
			const FSoundAttenuationSettings* AttenuationSettingsToApply = AttenuationNodes[NodeIndex]->GetAttenuationSettingsToApply();
			if (AttenuationSettingsToApply)
			{
				AttenuationSettingsToApply->CollectAttenuationShapesForVisualization(ShapeDetailsMap);
			}
		}
	}
}

float FActiveSound::GetAttenuationFrequency(const FSoundAttenuationSettings* Settings, const FAttenuationListenerData& ListenerData, const FVector2D& FrequencyRange, const FRuntimeFloatCurve& CustomCurve)
{
	float OutputFrequency = 0.0f;

	// If the frequency mapping is the same no matter what, no need to do any mapping
	if (FrequencyRange.X == FrequencyRange.Y)
	{
		OutputFrequency = FrequencyRange.X;
	}
	// If the transition band is instantaneous, just set it to before/after frequency value
	else if (Settings->LPFRadiusMin == Settings->LPFRadiusMax)
	{
		if (ListenerData.AttenuationDistance > Settings->LPFRadiusMin)
		{
			OutputFrequency = FrequencyRange.Y;
		}
		else
		{
			OutputFrequency = FrequencyRange.X;
		}
	}
	else if (Settings->AbsorptionMethod == EAirAbsorptionMethod::Linear)
	{
		FVector2D AbsorptionDistanceRange = { Settings->LPFRadiusMin, Settings->LPFRadiusMax };

		// Do log-scaling if we've been told to do so. This applies a log function to perceptually smooth filter frequency between target frequency ranges
		if (Settings->bEnableLogFrequencyScaling)
		{
			OutputFrequency = Audio::GetLogFrequencyClamped(ListenerData.AttenuationDistance, AbsorptionDistanceRange, FrequencyRange);
		}
		else
		{
			OutputFrequency = FMath::GetMappedRangeValueClamped(AbsorptionDistanceRange, FrequencyRange, ListenerData.AttenuationDistance);
		}
	}
	else
	{
		// In manual absorption mode, the frequency ranges are interpreted as a true "range"
		FVector2D ActualFreqRange(FMath::Min(FrequencyRange.X, FrequencyRange.Y), FMath::Max(FrequencyRange.X, FrequencyRange.Y));

		// Normalize the distance values to a value between 0 and 1
		FVector2D AbsorptionDistanceRange = { Settings->LPFRadiusMin, Settings->LPFRadiusMax };
		check(AbsorptionDistanceRange.Y != AbsorptionDistanceRange.X);
		const float Alpha = FMath::Clamp<float>((ListenerData.AttenuationDistance - AbsorptionDistanceRange.X) / (AbsorptionDistanceRange.Y - AbsorptionDistanceRange.X), 0.0f, 1.0f);

		// Perform the curve mapping
		const float MappedFrequencyValue = FMath::Clamp<float>(CustomCurve.GetRichCurveConst()->Eval(Alpha), 0.0f, 1.0f);

		if (Settings->bEnableLogFrequencyScaling)
		{
			// Use the mapped value in the log scale mapping
			OutputFrequency = Audio::GetLogFrequencyClamped(MappedFrequencyValue, FVector2D(0.0f, 1.0f), ActualFreqRange);
		}
		else
		{
			// Do a straight linear interpolation between the absorption frequency ranges
			OutputFrequency = FMath::GetMappedRangeValueClamped(FVector2D(0.0f, 1.0f), ActualFreqRange, MappedFrequencyValue);
		}
	}

	return FMath::Clamp<float>(OutputFrequency, MIN_FILTER_FREQUENCY, MAX_FILTER_FREQUENCY);
}

void FActiveSound::UpdateFocusData(float DeltaTime, const FAttenuationListenerData& ListenerData, FAttenuationFocusData* OutFocusData)
{
	FAttenuationFocusData* FocusDataToUpdate = OutFocusData ? OutFocusData : &FocusData;

	AudioDevice->GetAzimuth(ListenerData, FocusDataToUpdate->Azimuth, FocusDataToUpdate->AbsoluteAzimuth);

	FocusDataToUpdate->DistanceScale = 1.0f;
	FocusDataToUpdate->PriorityScale = 1.0f;

	if (!ListenerData.AttenuationSettings->bEnableListenerFocus)
	{
		return;
	}

	if (!ListenerData.AttenuationSettings->bSpatialize)
	{
		return;
	}

	const FGlobalFocusSettings& FocusSettings = AudioDevice->GetGlobalFocusSettings();
	const float TargetFocusFactor = AudioDevice->GetFocusFactor(FocusDataToUpdate->Azimuth, *ListenerData.AttenuationSettings);

	// User opt-in for focus interpolation
	if (ListenerData.AttenuationSettings->bEnableFocusInterpolation)
	{
		// Determine which interpolation speed to use (attack/release)
		float InterpSpeed;
		if (TargetFocusFactor <= FocusDataToUpdate->FocusFactor)
		{
			InterpSpeed = ListenerData.AttenuationSettings->FocusAttackInterpSpeed;
		}
		else
		{
			InterpSpeed = ListenerData.AttenuationSettings->FocusReleaseInterpSpeed;
		}

		FocusDataToUpdate->FocusFactor = FMath::FInterpTo(FocusDataToUpdate->FocusFactor, TargetFocusFactor, DeltaTime, InterpSpeed);
	}
	else
	{
		// Set focus directly to target value
		FocusDataToUpdate->FocusFactor = TargetFocusFactor;
	}

	// Scale the volume-weighted priority scale value we use for sorting this sound for voice-stealing
	FocusDataToUpdate->PriorityScale = ListenerData.AttenuationSettings->GetFocusPriorityScale(FocusSettings, FocusDataToUpdate->FocusFactor);
	FocusDataToUpdate->DistanceScale = ListenerData.AttenuationSettings->GetFocusDistanceScale(FocusSettings, FocusDataToUpdate->FocusFactor);
	FocusDataToUpdate->VolumeScale = ListenerData.AttenuationSettings->GetFocusAttenuation(FocusSettings, FocusDataToUpdate->FocusFactor);
}

void FActiveSound::ParseAttenuation(FSoundParseParameters& OutParseParams, const FListener& InListener, const FSoundAttenuationSettings& InAttenuationSettings)
{
	UpdateAttenuation(0.0f, OutParseParams, InListener.ListenerIndex, &InAttenuationSettings);
}

void FActiveSound::ParseAttenuation(FSoundParseParameters& OutParseParams, int32 ListenerIndex, const FSoundAttenuationSettings& InAttenuationSettings)
{
	UpdateAttenuation(0.0f, OutParseParams, ListenerIndex, &InAttenuationSettings);
}

void FActiveSound::UpdateAttenuation(float DeltaTime, FSoundParseParameters& ParseParams, const FListener& Listener, const FSoundAttenuationSettings* SettingsAttenuationNode)
{
	UpdateAttenuation(DeltaTime, ParseParams, Listener.ListenerIndex, SettingsAttenuationNode);
}

void FActiveSound::UpdateAttenuation(float DeltaTime, FSoundParseParameters& ParseParams, int32 ListenerIndex, const FSoundAttenuationSettings* SettingsAttenuationNode)
{
	// Get the attenuation settings to use for this application to the active sound
	const FSoundAttenuationSettings* Settings = SettingsAttenuationNode ? SettingsAttenuationNode : &AttenuationSettings;

	// Reset Focus data and recompute if necessary
	FAttenuationFocusData FocusDataToApply;
	FocusDataToApply.PriorityHighest = FocusData.PriorityHighest;

	if (Settings->bEnableReverbSend)
	{
		ParseParams.ReverbSendMethod = Settings->ReverbSendMethod;
		ParseParams.ManualReverbSendLevel = Settings->ManualReverbSendLevel;
		ParseParams.CustomReverbSendCurve = Settings->CustomReverbSendCurve;
		ParseParams.ReverbSendLevelRange = { Settings->ReverbWetLevelMin, Settings->ReverbWetLevelMax };
		ParseParams.ReverbSendLevelDistanceRange = { Settings->ReverbDistanceMin, Settings->ReverbDistanceMax };
	}

	check(AudioDevice);
	FAttenuationListenerData ListenerData = FAttenuationListenerData::Create(*AudioDevice, ListenerIndex, ParseParams.Transform, *Settings);

	// Apply priority attenuation if it's enabled
	if (Settings->bEnablePriorityAttenuation)
	{
		float PriorityScale = 1.0f;
		if (Settings->PriorityAttenuationMethod == EPriorityAttenuationMethod::Manual)
		{
			PriorityScale = Settings->ManualPriorityAttenuation;
		}
		else
		{
			const float Denom = FMath::Max(Settings->PriorityAttenuationDistanceMax - Settings->PriorityAttenuationDistanceMin, 1.0f);
			const float Alpha = FMath::Clamp((ListenerData.ListenerToSoundDistance - Settings->PriorityAttenuationDistanceMin) / Denom, 0.0f, 1.0f);

			if (Settings->PriorityAttenuationMethod == EPriorityAttenuationMethod::Linear)
			{
				PriorityScale = FMath::Clamp(FMath::Lerp(Settings->PriorityAttenuationMin, Settings->PriorityAttenuationMax, Alpha), 0.0f, 1.0f);
			}
			else
			{
				PriorityScale = FMath::Clamp(Settings->CustomPriorityAttenuationCurve.GetRichCurveConst()->Eval(Alpha), 0.0f, 1.0f);
			}
		}

		ParseParams.Priority *= FMath::Max(PriorityScale, 0.0f);
		ParseParams.Priority = FMath::Clamp(ParseParams.Priority, 0.0f, 100.0f);
	}

	if (Settings->bSpatialize || Settings->bEnableListenerFocus)
	{
		// Feed prior focus factor on update to allow for proper interpolation.
		FocusDataToApply.FocusFactor = FocusData.FocusFactor;

		// Update azimuth angles prior to updating focus as it uses this in calculating
		// in and out of focus values.
		UpdateFocusData(DeltaTime, ListenerData, &FocusDataToApply);

		// Update FocusData's highest priority copy prior to applying cached scalar immediately following
		// to avoid applying scalar twice
		FocusDataToApply.PriorityHighest = FMath::Max(FocusDataToApply.PriorityHighest, ParseParams.Priority);

		ParseParams.Volume *= FocusDataToApply.VolumeScale;
		ParseParams.Priority *= FocusDataToApply.PriorityScale;
		if (Settings->bSpatialize)
		{
			ParseParams.AttenuationDistance = ListenerData.AttenuationDistance;
			ParseParams.ListenerToSoundDistance = ListenerData.ListenerToSoundDistance;
			ParseParams.ListenerToSoundDistanceForPanning = ListenerData.ListenerToSoundDistanceForPanning;
			ParseParams.AbsoluteAzimuth = FocusDataToApply.AbsoluteAzimuth;
		}
	}

	// Attenuate the volume based on the model. Note we don't apply the distance attenuation immediately to the sound.
	// The audio mixer applies distance-based attenuation as a separate stage to feed source audio through source effects and buses.
	// The old audio engine will scale this together when the wave instance is queried for GetActualVolume.
	if (Settings->bAttenuate)
	{
		if (Settings->AttenuationShape == EAttenuationShape::Sphere)
		{
			// Update attenuation data in-case it hasn't been updated
			ParseParams.DistanceAttenuation *= Settings->AttenuationEval(ListenerData.AttenuationDistance, Settings->FalloffDistance, FocusDataToApply.DistanceScale);
		}
		else
		{
			const FVector ListenerTranslation = ListenerData.ListenerTransform.GetTranslation();
			ParseParams.DistanceAttenuation *= Settings->Evaluate(ParseParams.Transform, ListenerTranslation, FocusDataToApply.DistanceScale);
		}
	}

	// Only do occlusion traces if the sound is audible and we're not using a occlusion plugin
	if (Settings->bEnableOcclusion)
	{
		// If we've got a occlusion plugin settings, then the plugin will handle occlusion calculations
		if (Settings->PluginSettings.OcclusionPluginSettingsArray.Num())
		{
			UClass* PluginClass = GetAudioPluginCustomSettingsClass(EAudioPlugin::OCCLUSION);
			if (PluginClass)
			{
				for (UOcclusionPluginSourceSettingsBase* SettingsBase : Settings->PluginSettings.OcclusionPluginSettingsArray)
				{
					if (SettingsBase != nullptr && SettingsBase->IsA(PluginClass))
					{
						ParseParams.OcclusionPluginSettings = SettingsBase;
						break;
					}
				}
			}
		}
		else if (ParseParams.Volume > 0.0f && !AudioDevice->IsAudioDeviceMuted())
		{
			check(ClosestListenerIndex != INDEX_NONE);
			FVector ListenerPosition;
			const bool bAllowOverride = false;
			AudioDevice->GetListenerPosition(ClosestListenerIndex, ListenerPosition, bAllowOverride);
			CheckOcclusion(ListenerPosition, ParseParams.Transform.GetTranslation(), Settings);

			// Apply the volume attenuation due to occlusion (using the interpolating dynamic parameter)
			ParseParams.DistanceAttenuation *= CurrentOcclusionVolumeAttenuation.GetValue();

			ParseParams.bIsOccluded = bIsOccluded;
			ParseParams.OcclusionFilterFrequency = CurrentOcclusionFilterFrequency.GetValue();
		}
	}

	// Figure out which attenuation settings to use
	if (Settings->PluginSettings.SpatializationPluginSettingsArray.Num() > 0)
	{
		UClass* PluginClass = GetAudioPluginCustomSettingsClass(EAudioPlugin::SPATIALIZATION);
		if (PluginClass)
		{
			for (USpatializationPluginSourceSettingsBase* SettingsBase : Settings->PluginSettings.SpatializationPluginSettingsArray)
			{
				if (SettingsBase != nullptr && SettingsBase->IsA(PluginClass))
				{
					ParseParams.SpatializationPluginSettings = SettingsBase;
					break;
				}
			}
		}
	}

	if (Settings->PluginSettings.ReverbPluginSettingsArray.Num() > 0)
	{
		UClass* PluginClass = GetAudioPluginCustomSettingsClass(EAudioPlugin::REVERB);
		if (PluginClass)
		{
			for (UReverbPluginSourceSettingsBase* SettingsBase : Settings->PluginSettings.ReverbPluginSettingsArray)
			{
				if (SettingsBase != nullptr && SettingsBase->IsA(PluginClass))
				{
					ParseParams.ReverbPluginSettings = SettingsBase;
					break;
				}
			}
		}
	}

	// Attenuate with the absorption filter if necessary
	if (Settings->bAttenuateWithLPF)
	{
		FVector2D AbsorptionLowPassFrequencyRange = { Settings->LPFFrequencyAtMin, Settings->LPFFrequencyAtMax };
		FVector2D AbsorptionHighPassFrequencyRange = { Settings->HPFFrequencyAtMin, Settings->HPFFrequencyAtMax };
		const float AttenuationLowpassFilterFrequency = GetAttenuationFrequency(Settings, ListenerData, AbsorptionLowPassFrequencyRange, Settings->CustomLowpassAirAbsorptionCurve);
		const float AttenuationHighPassFilterFrequency = GetAttenuationFrequency(Settings, ListenerData, AbsorptionHighPassFrequencyRange, Settings->CustomHighpassAirAbsorptionCurve);

		// Only apply the attenuation filter frequency if it results in a lower attenuation filter frequency than is already being used by ParseParams (the struct pass into the sound cue node tree)
		// This way, subsequently chained attenuation nodes in a sound cue will only result in the lowest frequency of the set.
		if (AttenuationLowpassFilterFrequency < ParseParams.AttenuationLowpassFilterFrequency)
		{
			ParseParams.AttenuationLowpassFilterFrequency = AttenuationLowpassFilterFrequency;
		}

		// Same with high pass filter frequency
		if (AttenuationHighPassFilterFrequency > ParseParams.AttenuationHighpassFilterFrequency)
		{
			ParseParams.AttenuationHighpassFilterFrequency = AttenuationHighPassFilterFrequency;
		}
	}

	ParseParams.OmniRadius = Settings->OmniRadius;
	ParseParams.StereoSpread = Settings->StereoSpread;
	ParseParams.bApplyNormalizationToStereoSounds = Settings->bApplyNormalizationToStereoSounds;
	ParseParams.bUseSpatialization |= Settings->bSpatialize;

	// Check the binaural radius to determine if we're going to HRTF spatialize
	if (ListenerData.ListenerToSoundDistance < Settings->BinauralRadius)
	{
		ParseParams.SpatializationMethod = ESoundSpatializationAlgorithm::SPATIALIZATION_Default;
	}
	else
	{
		if (Settings->SpatializationAlgorithm == ESoundSpatializationAlgorithm::SPATIALIZATION_Default && AudioDevice->IsHRTFEnabledForAll())
		{
			ParseParams.SpatializationMethod = ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF;
		}
		else if (Settings->SpatializationAlgorithm == ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF && AudioDevice->IsHRTFDisabled())
		{
			ParseParams.SpatializationMethod = ESoundSpatializationAlgorithm::SPATIALIZATION_Default;
		}
		else
		{
			ParseParams.SpatializationMethod = Settings->SpatializationAlgorithm;
		}
	}

	// If not overriding from a node, set focus data
	if (!SettingsAttenuationNode)
	{
		FocusData = FocusDataToApply;
	}
	// Make sure to always update highest priority
	else
	{
		FocusData.PriorityHighest = FocusDataToApply.PriorityHighest;
	}
}
