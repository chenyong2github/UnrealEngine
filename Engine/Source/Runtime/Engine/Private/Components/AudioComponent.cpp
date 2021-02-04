// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/AudioComponent.h"
#include "Audio.h"
#include "Engine/Texture2D.h"
#include "ActiveSound.h"
#include "AudioThread.h"
#include "AudioDevice.h"
#include "DSP/VolumeFader.h"
#include "Sound/SoundNodeAttenuation.h"
#include "Sound/SoundCue.h"
#include "Components/BillboardComponent.h"
#include "UObject/FrameworkObjectVersion.h"
#include "Misc/App.h"
#include "Kismet/GameplayStatics.h"



DECLARE_CYCLE_STAT(TEXT("AudioComponent Play"), STAT_AudioComp_Play, STATGROUP_Audio);

static float BakedAnalysisTimeShiftCVar = 0.0f;
FAutoConsoleVariableRef CVarBackedAnalysisTimeShift(
	TEXT("au.AnalysisTimeShift"),
	BakedAnalysisTimeShiftCVar,
	TEXT("Shifts the timeline for baked analysis playback.\n")
	TEXT("Value: The time in seconds to shift the timeline."),
	ECVF_Default);

static int32 PrimeSoundOnAudioComponentSpawnCVar = 0;
FAutoConsoleVariableRef CVarPrimeSoundOnAudioComponentSpawn(
	TEXT("au.streamcaching.PrimeSoundOnAudioComponents"),
	PrimeSoundOnAudioComponentSpawnCVar,
	TEXT("When set to 1, automatically primes a USoundBase when a UAudioComponent is spawned with that sound, or when UAudioComponent::SetSound is called.\n"),
	ECVF_Default);


/*-----------------------------------------------------------------------------
UAudioComponent implementation.
-----------------------------------------------------------------------------*/
uint64 UAudioComponent::AudioComponentIDCounter = 0;
TMap<uint64, UAudioComponent*> UAudioComponent::AudioIDToComponentMap;
FCriticalSection UAudioComponent::AudioIDToComponentMapLock;

UAudioComponent::UAudioComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bUseAttachParentBound = true; // Avoid CalcBounds() when transform changes.
	bAutoDestroy = false;
	bAutoManageAttachment = false;
	bAutoActivate = true;
	bAllowAnyoneToDestroyMe = true;
	bAllowSpatialization = true;
	bStopWhenOwnerDestroyed = true;
	bNeverNeedsRenderUpdate = true;
	bWantsOnUpdateTransform = true;
#if WITH_EDITORONLY_DATA
	bVisualizeComponent = true;
#endif
	VolumeMultiplier = 1.f;
	bOverridePriority = false;
	bOverrideSubtitlePriority = false;
	bIsPreviewSound = false;
	bIsPaused = false;

	Priority = 1.f;
	SubtitlePriority = DEFAULT_SUBTITLE_PRIORITY;
	PitchMultiplier = 1.f;
	VolumeModulationMin = 1.f;
	VolumeModulationMax = 1.f;
	PitchModulationMin = 1.f;
	PitchModulationMax = 1.f;
	bEnableLowPassFilter = false;
	LowPassFilterFrequency = MAX_FILTER_FREQUENCY;
	OcclusionCheckInterval = 0.1f;
	ActiveCount = 0;

	EnvelopeFollowerAttackTime = 10;
	EnvelopeFollowerReleaseTime = 100;

	AudioDeviceID = INDEX_NONE;
	AudioComponentID = FPlatformAtomics::InterlockedIncrement(reinterpret_cast<volatile int64*>(&AudioComponentIDCounter));

	RandomStream.Initialize(FApp::bUseFixedSeed ? GetFName() : NAME_None);

	{
		// TODO: Consider only putting played/active components in to the map
		FScopeLock Lock(&AudioIDToComponentMapLock);
		AudioIDToComponentMap.Add(AudioComponentID, this);
	}
}

UAudioComponent* UAudioComponent::GetAudioComponentFromID(uint64 AudioComponentID)
{
	//although we should be in the game thread when calling this function, async loading makes it possible/common for these
	//components to be constructed outside of the game thread. this means we need a lock around anything that deals with the
	//AudioIDToComponentMap.
	FScopeLock Lock(&AudioIDToComponentMapLock);
	return AudioIDToComponentMap.FindRef(AudioComponentID);
}

void UAudioComponent::BeginDestroy()
{
	Super::BeginDestroy();

	if (IsActive() && Sound && Sound->IsLooping())
	{
		UE_LOG(LogAudio, Verbose, TEXT("Audio Component is being destroyed prior to stopping looping sound '%s' directly."), *Sound->GetFullName());
		Stop();
	}

	FScopeLock Lock(&AudioIDToComponentMapLock);
	AudioIDToComponentMap.Remove(AudioComponentID);
}

FString UAudioComponent::GetDetailedInfoInternal() const
{
	FString Result;

	if (Sound != nullptr)
	{
		Result = Sound->GetPathName(nullptr);
	}
	else
	{
		Result = TEXT( "No_Sound" );
	}

	return Result;
}

void UAudioComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);

	if (Ar.IsLoading() && Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::ChangeAudioComponentOverrideSubtitlePriorityDefault)
	{
		// Since the default for overriding the priority changed delta serialize would not have written out anything for true, so if they've changed
		// the priority we'll assume they wanted true, otherwise, we'll leave it with the new false default
		if (SubtitlePriority != DEFAULT_SUBTITLE_PRIORITY)
		{
			bOverrideSubtitlePriority = true;
		}
	}

#if WITH_EDITORONLY_DATA
	if (Ar.IsLoading())
	{
		if (ConcurrencySettings_DEPRECATED != nullptr)
		{
			ConcurrencySet.Add(ConcurrencySettings_DEPRECATED);
			ConcurrencySettings_DEPRECATED = nullptr;
		}
	}
#endif // WITH_EDITORONLY_DATA
}

void UAudioComponent::PostLoad()
{
#if WITH_EDITORONLY_DATA
	const int32 LinkerUE4Version = GetLinkerUE4Version();

	// Translate the old HighFrequencyGainMultiplier value to the new LowPassFilterFrequency value
	if (LinkerUE4Version < VER_UE4_USE_LOW_PASS_FILTER_FREQ)
	{
		if (HighFrequencyGainMultiplier_DEPRECATED > 0.0f &&  HighFrequencyGainMultiplier_DEPRECATED < 1.0f)
		{
			bEnableLowPassFilter = true;

			// This seems like it wouldn't make sense, but the original implementation for HighFrequencyGainMultiplier (a number between 0.0 and 1.0).
			// In earlier versions, this was *not* used as a high frequency gain, but instead converted to a frequency value between 0.0 and 6000.0
			// then "converted" to a radian frequency value using an equation taken from XAudio2 documentation. To recover
			// the original intended frequency (approximately), we'll run it through that equation, then scale radian value by the max filter frequency.

			float FilterConstant = 2.0f * FMath::Sin(PI * 6000.0f * HighFrequencyGainMultiplier_DEPRECATED / 48000);
			LowPassFilterFrequency = FilterConstant * MAX_FILTER_FREQUENCY;
		}
	}
#endif

	if (PrimeSoundOnAudioComponentSpawnCVar && Sound)
	{
		UGameplayStatics::PrimeSound(Sound);
	}

	Super::PostLoad();
}

void UAudioComponent::OnRegister()
{
	if (bAutoManageAttachment && !IsActive())
	{
		// Detach from current parent, we are supposed to wait for activation.
		if (GetAttachParent())
		{
			// If no auto attach parent override, use the current parent when we activate
			if (!AutoAttachParent.IsValid())
			{
				AutoAttachParent = GetAttachParent();
			}
			// If no auto attach socket override, use current socket when we activate
			if (AutoAttachSocketName == NAME_None)
			{
				AutoAttachSocketName = GetAttachSocketName();
			}

			// If in a game world, detach now if necessary. Activation will cause auto-attachment.
			const UWorld* World = GetWorld();
			if (World->IsGameWorld())
			{
				// Prevent attachment before Super::OnRegister() tries to attach us, since we only attach when activated.
				if (GetAttachParent()->GetAttachChildren().Contains(this))
				{
					// Only detach if we are not about to auto attach to the same target, that would be wasteful.
					if (!bAutoActivate || (AutoAttachLocationRule != EAttachmentRule::KeepRelative && AutoAttachRotationRule != EAttachmentRule::KeepRelative && AutoAttachScaleRule != EAttachmentRule::KeepRelative) || (AutoAttachSocketName != GetAttachSocketName()) || (AutoAttachParent != GetAttachParent()))
					{
						DetachFromComponent(FDetachmentTransformRules(EDetachmentRule::KeepRelative, /*bCallModify=*/ false));
					}
				}
				else
				{
					SetupAttachment(nullptr, NAME_None);
				}
			}
		}

		SavedAutoAttachRelativeLocation = GetRelativeLocation();
		SavedAutoAttachRelativeRotation = GetRelativeRotation();
		SavedAutoAttachRelativeScale3D = GetRelativeScale3D();
	}

	Super::OnRegister();

	#if WITH_EDITORONLY_DATA
	UpdateSpriteTexture();
	#endif
}

void UAudioComponent::OnUnregister()
{
	// Route OnUnregister event.
	Super::OnUnregister();

	// Don't stop audio and clean up component if owner has been destroyed (default behaviour). This function gets
	// called from AActor::ClearComponents when an actor gets destroyed which is not usually what we want for one-
	// shot sounds.
	AActor* Owner = GetOwner();
	if (!Owner || bStopWhenOwnerDestroyed)
	{
		Stop();
	}
}

const UObject* UAudioComponent::AdditionalStatObject() const
{
	return Sound;
}

void UAudioComponent::SetSound(USoundBase* NewSound)
{
	const bool bPlay = IsPlaying();

	// If this is an auto destroy component we need to prevent it from being auto-destroyed since we're really just restarting it
	const bool bWasAutoDestroy = bAutoDestroy;
	bAutoDestroy = false;
	Stop();
	bAutoDestroy = bWasAutoDestroy;

	Sound = NewSound;

	if (PrimeSoundOnAudioComponentSpawnCVar && Sound)
	{
		UGameplayStatics::PrimeSound(Sound);
	}

	if (bPlay)
	{
		Play();
	}
}

bool UAudioComponent::IsReadyForOwnerToAutoDestroy() const
{
	return !IsPlaying();
}

void UAudioComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	Super::OnUpdateTransform(UpdateTransformFlags, Teleport);

	if (bPreviewComponent)
	{
		return;
	}

	if (FAudioDevice* AudioDevice = GetAudioDevice())
	{
		if (IsActive())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.UpdateAudioComponentTransform"), STAT_AudioUpdateComponentTransform, STATGROUP_AudioThreadCommands);

			const uint64 MyAudioComponentID = AudioComponentID;
			const FTransform& MyTransform = GetComponentTransform();

			FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, MyTransform]()
			{
				FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID);
				if (ActiveSound)
				{
					ActiveSound->Transform = MyTransform;

				}
			}, GET_STATID(STAT_AudioUpdateComponentTransform));
		}
	}
};

void UAudioComponent::BroadcastPlayState()
{
	if (OnAudioPlayStateChanged.IsBound())
	{
		OnAudioPlayStateChanged.Broadcast(GetPlayState());
	}

	if (OnAudioPlayStateChangedNative.IsBound())
	{
		OnAudioPlayStateChangedNative.Broadcast(this, GetPlayState());
	}
}

FBoxSphereBounds UAudioComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	const USceneComponent* UseAutoParent = (bAutoManageAttachment && GetAttachParent() == nullptr) ? AutoAttachParent.Get() : nullptr;
	if (UseAutoParent)
	{
		// We use auto attachment but have detached, don't use our own bogus bounds (we're off near 0,0,0), use the usual parent's bounds.
		return UseAutoParent->Bounds;
	}

	return Super::CalcBounds(LocalToWorld);
}

void UAudioComponent::CancelAutoAttachment(bool bDetachFromParent, const UWorld* MyWorld)
{
	if (bAutoManageAttachment && MyWorld && MyWorld->IsGameWorld())
	{
		if (bDidAutoAttach)
		{
			// Restore relative transform from before attachment. Actual transform will be updated as part of DetachFromParent().
			SetRelativeLocation_Direct(SavedAutoAttachRelativeLocation);
			SetRelativeRotation_Direct(SavedAutoAttachRelativeRotation);
			SetRelativeScale3D_Direct(SavedAutoAttachRelativeScale3D);
			bDidAutoAttach = false;
		}

		if (bDetachFromParent)
		{
			DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
		}
	}
}

bool UAudioComponent::IsInAudibleRange(float* OutMaxDistance) const
{
	FAudioDevice* AudioDevice = GetAudioDevice();
	if (!AudioDevice)
	{
		return false;
	}

	float MaxDistance = 0.0f;
	float FocusFactor = 0.0f;
	const FVector Location = GetComponentTransform().GetLocation();
	const FSoundAttenuationSettings* AttenuationSettingsToApply = bAllowSpatialization ? GetAttenuationSettingsToApply() : nullptr;
	AudioDevice->GetMaxDistanceAndFocusFactor(Sound, GetWorld(), Location, AttenuationSettingsToApply, MaxDistance, FocusFactor);

	if (OutMaxDistance)
	{
		*OutMaxDistance = MaxDistance;
	}

	return AudioDevice->SoundIsAudible(Sound, GetWorld(), Location, AttenuationSettingsToApply, MaxDistance, FocusFactor);
}

void UAudioComponent::Play(float StartTime)
{
	PlayInternalRequestData Data;
	Data.StartTime = StartTime;
	PlayInternal(Data);
}

void UAudioComponent::PlayQuantized(
	  const UObject* WorldContextObject
	, UPARAM(ref) UQuartzClockHandle*& InClockHandle
	, UPARAM(ref) FQuartzQuantizationBoundary& InQuantizationBoundary
	, const FOnQuartzCommandEventBP& InDelegate
	, float InStartTime
	, float InFadeInDuration
	, float InFadeVolumeLevel
	, EAudioFaderCurve InFadeCurve)
{
	PlayInternalRequestData Data;

	Data.StartTime = InStartTime;
	Data.FadeInDuration = InFadeInDuration;
	Data.FadeVolumeLevel = InFadeVolumeLevel;
	Data.FadeCurve = InFadeCurve;

	if (InClockHandle != nullptr)
	{
		Data.QuantizedRequestData = InClockHandle->GetQuartzSubsystem()->CreateDataDataForSchedulePlaySound(InClockHandle, InDelegate, InQuantizationBoundary);
		UGameplayStatics::PrimeSound(Sound);
	}

	// validate clock existence 
	if (!InClockHandle)
	{
		UE_LOG(LogAudio, Warning, TEXT("Attempting to play Quantized Sound without supplying a Clock Handle"));	
	}
	else if (!InClockHandle->DoesClockExist(WorldContextObject))
	{
		UE_LOG(LogAudio, Warning, TEXT("Clock: '%s' Does not exist! Cannot play quantized sound: %s"), *Data.QuantizedRequestData.ClockName.ToString(), *this->Sound->GetName());
		Data.QuantizedRequestData = {};
	}

	PlayInternal(Data);
}

void UAudioComponent::PlayInternal(const PlayInternalRequestData& InPlayRequestData)
{
	SCOPE_CYCLE_COUNTER(STAT_AudioComp_Play);

	UWorld* World = GetWorld();

	UE_LOG(LogAudio, Verbose, TEXT("%g: Playing AudioComponent : '%s' with Sound: '%s'"), World ? World->GetAudioTimeSeconds() : 0.0f, *GetFullName(), Sound ? *Sound->GetName() : TEXT("nullptr"));

	// Reset our fading out flag in case this is a reused audio component and we are replaying after previously fading out
	bIsFadingOut = false;

	if (IsActive())
	{
		// If this is an auto destroy component we need to prevent it from being auto-destroyed since we're really just restarting it
		bool bCurrentAutoDestroy = bAutoDestroy;
		bAutoDestroy = false;
		Stop();
		bAutoDestroy = bCurrentAutoDestroy;
	}

	// Whether or not we managed to actually try to play the sound
	if (Sound && (World == nullptr || World->bAllowAudioPlayback))
	{
		if (FAudioDevice* AudioDevice = GetAudioDevice())
		{
			// Store the time that this audio component played
			if (World)
			{
				TimeAudioComponentPlayed = World->GetAudioTimeSeconds();
			}
			else
			{
				TimeAudioComponentPlayed = 0.0f;
			}
			FadeInTimeDuration = InPlayRequestData.FadeInDuration;

			// Auto attach if requested
			const bool bWasAutoAttached = bDidAutoAttach;
			bDidAutoAttach = false;
			if (bAutoManageAttachment && World->IsGameWorld())
			{
				USceneComponent* NewParent = AutoAttachParent.Get();
				if (NewParent)
				{
					const bool bAlreadyAttached = GetAttachParent() && (GetAttachParent() == NewParent) && (GetAttachSocketName() == AutoAttachSocketName) && GetAttachParent()->GetAttachChildren().Contains(this);
					if (!bAlreadyAttached)
					{
						bDidAutoAttach = bWasAutoAttached;
						CancelAutoAttachment(true, World);
						SavedAutoAttachRelativeLocation = GetRelativeLocation();
						SavedAutoAttachRelativeRotation = GetRelativeRotation();
						SavedAutoAttachRelativeScale3D = GetRelativeScale3D();
						AttachToComponent(NewParent, FAttachmentTransformRules(AutoAttachLocationRule, AutoAttachRotationRule, AutoAttachScaleRule, false), AutoAttachSocketName);
					}

					bDidAutoAttach = true;
				}
				else
				{
					CancelAutoAttachment(true, World);
				}
			}

			// Create / configure new ActiveSound
			const FSoundAttenuationSettings* AttenuationSettingsToApply = bAllowSpatialization ? GetAttenuationSettingsToApply() : nullptr;

			float MaxDistance = 0.0f;
			float FocusFactor = 1.0f;
			FVector Location = GetComponentTransform().GetLocation();

			AudioDevice->GetMaxDistanceAndFocusFactor(Sound, World, Location, AttenuationSettingsToApply, MaxDistance, FocusFactor);

			FActiveSound NewActiveSound;
			NewActiveSound.SetAudioComponent(*this);
			NewActiveSound.SetWorld(GetWorld());
			NewActiveSound.SetSound(Sound);
			NewActiveSound.SetSourceEffectChain(SourceEffectChain);
			NewActiveSound.SetSoundClass(SoundClassOverride);
			NewActiveSound.ConcurrencySet = ConcurrencySet;

			const float Volume = (VolumeModulationMax + ((VolumeModulationMin - VolumeModulationMax) * RandomStream.FRand())) * VolumeMultiplier;
			NewActiveSound.SetVolume(Volume);

			// The priority used for the active sound is the audio component's priority scaled with the sound's priority
			if (bOverridePriority)
			{
				NewActiveSound.Priority = Priority;
			}
			else
			{
				NewActiveSound.Priority = Sound->Priority;
			}

			const float Pitch = (PitchModulationMax + ((PitchModulationMin - PitchModulationMax) * RandomStream.FRand())) * PitchMultiplier;
			NewActiveSound.SetPitch(Pitch);

			NewActiveSound.bEnableLowPassFilter = bEnableLowPassFilter;
			NewActiveSound.LowPassFilterFrequency = LowPassFilterFrequency;
			NewActiveSound.RequestedStartTime = FMath::Max(0.f, InPlayRequestData.StartTime);

			if (bOverrideSubtitlePriority)
			{
				NewActiveSound.SubtitlePriority = SubtitlePriority;
			}
			else
			{
				NewActiveSound.SubtitlePriority = Sound->GetSubtitlePriority();
			}

			NewActiveSound.bShouldRemainActiveIfDropped = bShouldRemainActiveIfDropped;
			NewActiveSound.bHandleSubtitles = (!bSuppressSubtitles || OnQueueSubtitles.IsBound());
			NewActiveSound.bIgnoreForFlushing = bIgnoreForFlushing;

			NewActiveSound.bIsUISound = bIsUISound;
			NewActiveSound.bIsMusic = bIsMusic;
			NewActiveSound.bAlwaysPlay = bAlwaysPlay;
			NewActiveSound.bReverb = bReverb;
			NewActiveSound.bCenterChannelOnly = bCenterChannelOnly;
			NewActiveSound.bIsPreviewSound = bIsPreviewSound;
			NewActiveSound.bLocationDefined = !bPreviewComponent;
			NewActiveSound.bIsPaused = bIsPaused;

			if (NewActiveSound.bLocationDefined)
			{
				NewActiveSound.Transform = GetComponentTransform();
			}

			NewActiveSound.bAllowSpatialization = bAllowSpatialization;
			NewActiveSound.bHasAttenuationSettings = (AttenuationSettingsToApply != nullptr);
			if (NewActiveSound.bHasAttenuationSettings)
			{
				NewActiveSound.AttenuationSettings = *AttenuationSettingsToApply;
				NewActiveSound.FocusData.PriorityScale = AttenuationSettingsToApply->GetFocusPriorityScale(AudioDevice->GetGlobalFocusSettings(), FocusFactor);
			}

			NewActiveSound.EnvelopeFollowerAttackTime = FMath::Max(EnvelopeFollowerAttackTime, 0);
			NewActiveSound.EnvelopeFollowerReleaseTime = FMath::Max(EnvelopeFollowerReleaseTime, 0);

			NewActiveSound.bUpdatePlayPercentage = OnAudioPlaybackPercentNative.IsBound() || OnAudioPlaybackPercent.IsBound();
			NewActiveSound.bUpdateSingleEnvelopeValue = OnAudioSingleEnvelopeValue.IsBound() || OnAudioSingleEnvelopeValueNative.IsBound();
			NewActiveSound.bUpdateMultiEnvelopeValue = OnAudioMultiEnvelopeValue.IsBound() || OnAudioMultiEnvelopeValueNative.IsBound();

			NewActiveSound.ModulationRouting = ModulationRouting;

			// Setup audio component cooked analysis data playback data set
			if (AudioDevice->IsBakedAnalaysisQueryingEnabled())
			{
				TArray<USoundWave*> SoundWavesWithCookedData;
				NewActiveSound.bUpdatePlaybackTime = Sound->GetSoundWavesWithCookedAnalysisData(SoundWavesWithCookedData);

				// Reset the audio component's soundwave playback times
				SoundWavePlaybackTimes.Reset();
				for (USoundWave* SoundWave : SoundWavesWithCookedData)
				{
					SoundWavePlaybackTimes.Add(SoundWave->GetUniqueID(), FSoundWavePlaybackTimeData(SoundWave));
				}
			}


			// Pass quantization data to the active sound
			NewActiveSound.QuantizedRequestData = InPlayRequestData.QuantizedRequestData;

			NewActiveSound.MaxDistance = MaxDistance;
			NewActiveSound.InstanceParameters = InstanceParameters;

			Audio::FVolumeFader& Fader = NewActiveSound.ComponentVolumeFader;
			Fader.SetVolume(0.0f); // Init to 0.0f to fade as default is 1.0f
			Fader.StartFade(InPlayRequestData.FadeVolumeLevel, InPlayRequestData.FadeInDuration, static_cast<Audio::EFaderCurve>(InPlayRequestData.FadeCurve));

			// Bump ActiveCount... this is used to determine if an audio component is still active after a sound reports back as completed
			++ActiveCount;
			AudioDevice->AddNewActiveSound(NewActiveSound);

			// In editor, the audio thread is not run separate from the game thread, and can result in calling PlaybackComplete prior
			// to bIsActive being set. Therefore, we assign to the current state of ActiveCount as opposed to just setting to true.
			SetActiveFlag(ActiveCount > 0);

			BroadcastPlayState();
		}
	}
}

FAudioDevice* UAudioComponent::GetAudioDevice() const
{
	FAudioDevice* AudioDevice = nullptr;

	if (GEngine)
	{
		if (AudioDeviceID != INDEX_NONE)
		{
			FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager();
			AudioDevice = (AudioDeviceManager ? AudioDeviceManager->GetAudioDeviceRaw(AudioDeviceID) : nullptr);
		}
		else if (UWorld* World = GetWorld())
		{
			AudioDevice = World->GetAudioDeviceRaw();
		}
		else
		{
			AudioDevice = GEngine->GetMainAudioDeviceRaw();
		}
	}
	return AudioDevice;
}

void UAudioComponent::FadeIn(float FadeInDuration, float FadeVolumeLevel, float StartTime, const EAudioFaderCurve FadeCurve)
{
	PlayInternalRequestData Data;
	Data.StartTime = StartTime;
	Data.FadeInDuration = FadeInDuration;
	Data.FadeVolumeLevel = FadeVolumeLevel;
	Data.FadeCurve = FadeCurve;

	PlayInternal(Data);
}

void UAudioComponent::FadeOut(float FadeOutDuration, float FadeVolumeLevel, const EAudioFaderCurve FadeCurve)
{
	const bool bIsFadeOut = true;
	AdjustVolumeInternal(FadeOutDuration, FadeVolumeLevel, bIsFadeOut, FadeCurve);
}

void UAudioComponent::AdjustVolume(float AdjustVolumeDuration, float AdjustVolumeLevel, const EAudioFaderCurve FadeCurve)
{
	const bool bIsFadeOut = false;
	AdjustVolumeInternal(AdjustVolumeDuration, AdjustVolumeLevel, bIsFadeOut, FadeCurve);
}

void UAudioComponent::AdjustVolumeInternal(float AdjustVolumeDuration, float AdjustVolumeLevel, bool bInIsFadeOut, const EAudioFaderCurve FadeCurve)
{
	if (!IsActive())
	{
		return;
	}

	FAudioDevice* AudioDevice = GetAudioDevice();
	if (!AudioDevice)
	{
		return;
	}

	AdjustVolumeDuration = FMath::Max(0.0f, AdjustVolumeDuration);
	AdjustVolumeLevel = FMath::Max(0.0f, AdjustVolumeLevel);
	if (FMath::IsNearlyZero(AdjustVolumeDuration) && FMath::IsNearlyZero(AdjustVolumeLevel))
	{
		Stop();
		return;
	}

	const bool bWasFadingOut = bIsFadingOut;
	bIsFadingOut = bInIsFadeOut || FMath::IsNearlyZero(AdjustVolumeLevel);

	if (bWasFadingOut != bIsFadingOut)
	{
		BroadcastPlayState();
	}

	const uint64 InAudioComponentID = AudioComponentID;
	DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.AdjustVolume"), STAT_AudioAdjustVolume, STATGROUP_AudioThreadCommands);
	FAudioThread::RunCommandOnAudioThread([AudioDevice, InAudioComponentID, AdjustVolumeDuration, AdjustVolumeLevel, bInIsFadeOut, FadeCurve]()
	{
		FActiveSound* ActiveSound = AudioDevice->FindActiveSound(InAudioComponentID);
		if (!ActiveSound)
		{
			return;
		}

		Audio::FVolumeFader& Fader = ActiveSound->ComponentVolumeFader;
		const float InitialTargetVolume = Fader.GetTargetVolume();

		// Ignore fade out request if requested volume is higher than current target.
		if (bInIsFadeOut && AdjustVolumeLevel >= InitialTargetVolume)
		{
			return;
		}

		const bool ToZeroVolume = FMath::IsNearlyZero(AdjustVolumeLevel);
		if (ActiveSound->FadeOut == FActiveSound::EFadeOut::Concurrency)
		{
			// Ignore adjust volume request if non-zero and currently voice stealing.
			if (!FMath::IsNearlyZero(AdjustVolumeLevel))
			{
				return;
			}

			// Ignore request of longer fade out than active target if active is concurrency (voice stealing) fade.
			if (AdjustVolumeDuration > Fader.GetFadeDuration())
			{
				return;
			}
		}
		else
		{
			ActiveSound->FadeOut = bInIsFadeOut || ToZeroVolume ? FActiveSound::EFadeOut::User : FActiveSound::EFadeOut::None;
		}

		if (bInIsFadeOut || ToZeroVolume)
		{
			// If negative, active indefinitely, so always make sure set to minimum positive value for active fade.
			const float OldActiveDuration = Fader.GetActiveDuration();
			const float NewActiveDuration = OldActiveDuration < 0.0f
				? AdjustVolumeDuration
				: FMath::Min(OldActiveDuration, AdjustVolumeDuration);
			Fader.SetActiveDuration(NewActiveDuration);
		}

		Fader.StartFade(AdjustVolumeLevel, AdjustVolumeDuration, static_cast<Audio::EFaderCurve>(FadeCurve));
	}, GET_STATID(STAT_AudioAdjustVolume));
}

void UAudioComponent::Stop()
{
	if (!IsActive())
	{
		return;
	}

	FAudioDevice* AudioDevice = GetAudioDevice();
	if (!AudioDevice)
	{
		return;
	}

	// Set this to immediately be inactive
	SetActiveFlag(false);

	UE_LOG(LogAudio, Verbose, TEXT("%g: Stopping AudioComponent : '%s' with Sound: '%s'"),
		GetWorld() ? GetWorld()->GetAudioTimeSeconds() : 0.0f, *GetFullName(),
		Sound ? *Sound->GetName() : TEXT("nullptr"));

	AudioDevice->StopActiveSound(AudioComponentID);

	BroadcastPlayState();
}

void UAudioComponent::StopDelayed(float DelayTime)
{
	// 1. Stop immediately if no delay time
	if (DelayTime < 0.0f || FMath::IsNearlyZero(DelayTime))
	{
		Stop();
		return;
	}

	if (!IsActive())
	{
		return;
	}

	FAudioDevice* AudioDevice = GetAudioDevice();
	if (!AudioDevice)
	{
		return;
	}

	// 2. Performs delayed stop with no fade
	const uint64 InAudioComponentID = AudioComponentID;
	DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.StopDelayed"), STAT_AudioStopDelayed, STATGROUP_AudioThreadCommands);
	FAudioThread::RunCommandOnAudioThread([AudioDevice, InAudioComponentID, DelayTime]()
	{
		FActiveSound* ActiveSound = AudioDevice->FindActiveSound(InAudioComponentID);
		if (!ActiveSound)
		{
			return;
		}

		if (const USoundBase* StoppingSound = ActiveSound->GetSound())
		{
			UE_LOG(LogAudio, Verbose, TEXT("%g: Delayed Stop requested for sound '%s'"),
				ActiveSound->GetWorld() ? ActiveSound->GetWorld()->GetAudioTimeSeconds() : 0.0f,
				*StoppingSound->GetName());
		}

		Audio::FVolumeFader& Fader = ActiveSound->ComponentVolumeFader;
		switch (ActiveSound->FadeOut)
		{
			case FActiveSound::EFadeOut::Concurrency:
			{
				// Ignore request of longer fade out than active target if active is concurrency (voice stealing) fade.
				if (DelayTime < Fader.GetFadeDuration())
				{
					Fader.SetActiveDuration(DelayTime);
				}
			}
			break;
			
			case FActiveSound::EFadeOut::User:
			case FActiveSound::EFadeOut::None:
			default:
			{
				ActiveSound->FadeOut = FActiveSound::EFadeOut::User;
				Fader.SetActiveDuration(DelayTime);
			}
			break;
		}
	}, GET_STATID(STAT_AudioStopDelayed));
}

void UAudioComponent::SetPaused(bool bPause)
{
	if (bIsPaused != bPause)
	{
		bIsPaused = bPause;

		if (IsActive())
		{
			UE_LOG(LogAudio, Verbose, TEXT("%g: Pausing AudioComponent : '%s' with Sound: '%s'"), GetWorld() ? GetWorld()->GetAudioTimeSeconds() : 0.0f, *GetFullName(), Sound ? *Sound->GetName() : TEXT("nullptr"));

			if (FAudioDevice* AudioDevice = GetAudioDevice())
			{
				DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.PauseActiveSound"), STAT_AudioPauseActiveSound, STATGROUP_AudioThreadCommands);

				const uint64 MyAudioComponentID = AudioComponentID;
				FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, bPause]()
				{
					AudioDevice->PauseActiveSound(MyAudioComponentID, bPause);
				}, GET_STATID(STAT_AudioPauseActiveSound));
			}
		}

		BroadcastPlayState();
	}
}

void UAudioComponent::PlaybackCompleted(uint64 AudioComponentID, bool bFailedToStart)
{
	check(IsInAudioThread());

	DECLARE_CYCLE_STAT(TEXT("FGameThreadAudioTask.PlaybackCompleted"), STAT_AudioPlaybackCompleted, STATGROUP_TaskGraphTasks);

	FAudioThread::RunCommandOnGameThread([AudioComponentID, bFailedToStart]()
	{
		if (UAudioComponent* AudioComponent = GetAudioComponentFromID(AudioComponentID))
		{
			AudioComponent->PlaybackCompleted(bFailedToStart);
		}
	}, GET_STATID(STAT_AudioPlaybackCompleted));
}

void UAudioComponent::PlaybackCompleted(bool bFailedToStart)
{
	check(ActiveCount > 0);
	--ActiveCount;

	if (ActiveCount > 0)
	{
		return;
	}

	// Mark inactive before calling destroy to avoid recursion
	SetActiveFlag(false);

	const UWorld* MyWorld = GetWorld();
	if (!bFailedToStart && MyWorld != nullptr && (OnAudioFinished.IsBound() || OnAudioFinishedNative.IsBound()))
	{
		INC_DWORD_STAT(STAT_AudioFinishedDelegatesCalled);
		SCOPE_CYCLE_COUNTER(STAT_AudioFinishedDelegates);

		OnAudioFinished.Broadcast();
		OnAudioFinishedNative.Broadcast(this);
	}

	// Auto destruction is handled via marking object for deletion.
	if (bAutoDestroy)
	{
		DestroyComponent();
	}
	// Otherwise see if we should detach ourself and wait until we're needed again
	else if (bAutoManageAttachment)
	{
		CancelAutoAttachment(true, MyWorld);
	}

	BroadcastPlayState();
}

bool UAudioComponent::IsPlaying() const
{
	return IsActive();
}

bool UAudioComponent::IsVirtualized() const
{
	return bIsVirtualized;
}

EAudioComponentPlayState UAudioComponent::GetPlayState() const
{
	UWorld* World = GetWorld();
	if (!IsActive() || !World)
	{
		return EAudioComponentPlayState::Stopped;
	}

	if (bIsPaused)
	{
		return EAudioComponentPlayState::Paused;
	}

	if (bIsFadingOut)
	{
		return EAudioComponentPlayState::FadingOut;
	}

	// Get the current audio time seconds and compare when it started and the fade in duration 
	float CurrentAudioTimeSeconds = World->GetAudioTimeSeconds();
	if (CurrentAudioTimeSeconds - TimeAudioComponentPlayed < FadeInTimeDuration)
	{
		return EAudioComponentPlayState::FadingIn;
	}

	// If we are not in any of the above states we are "playing"
	return EAudioComponentPlayState::Playing;
}

#if WITH_EDITORONLY_DATA
void UAudioComponent::UpdateSpriteTexture()
{
	if (SpriteComponent)
	{
		SpriteComponent->SpriteInfo.Category = TEXT("Sounds");
		SpriteComponent->SpriteInfo.DisplayName = NSLOCTEXT("SpriteCategory", "Sounds", "Sounds");

		if (bAutoActivate)
		{
			SpriteComponent->SetSprite(LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EditorResources/AudioIcons/S_AudioComponent_AutoActivate.S_AudioComponent_AutoActivate")));
		}
		else
		{
			SpriteComponent->SetSprite(LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EditorResources/AudioIcons/S_AudioComponent.S_AudioComponent")));
		}
	}
}
#endif

#if WITH_EDITOR
void UAudioComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (IsActive())
	{
		// If this is an auto destroy component we need to prevent it from being auto-destroyed since we're really just restarting it
		const bool bWasAutoDestroy = bAutoDestroy;
		bAutoDestroy = false;
		Stop();
		bAutoDestroy = bWasAutoDestroy;
		Play();
	}

#if WITH_EDITORONLY_DATA
	UpdateSpriteTexture();
#endif

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR

const FSoundAttenuationSettings* UAudioComponent::GetAttenuationSettingsToApply() const
{
	if (bOverrideAttenuation)
	{
		return &AttenuationOverrides;
	}
	else if (AttenuationSettings)
	{
		return &AttenuationSettings->Attenuation;
	}
	else if (Sound)
	{
		return Sound->GetAttenuationSettingsToApply();
	}
	return nullptr;
}

bool UAudioComponent::BP_GetAttenuationSettingsToApply(FSoundAttenuationSettings& OutAttenuationSettings)
{
	if (const FSoundAttenuationSettings* Settings = GetAttenuationSettingsToApply())
	{
		OutAttenuationSettings = *Settings;
		return true;
	}
	return false;
}

void UAudioComponent::CollectAttenuationShapesForVisualization(TMultiMap<EAttenuationShape::Type, FBaseAttenuationSettings::AttenuationShapeDetails>& ShapeDetailsMap) const
{
	const FSoundAttenuationSettings *AttenuationSettingsToApply = GetAttenuationSettingsToApply();

	if (AttenuationSettingsToApply)
	{
		AttenuationSettingsToApply->CollectAttenuationShapesForVisualization(ShapeDetailsMap);
	}

	// For sound cues we'll dig in and see if we can find any attenuation sound nodes that will affect the settings
	USoundCue* SoundCue = Cast<USoundCue>(Sound);
	if (SoundCue)
	{
		TArray<USoundNodeAttenuation*> AttenuationNodes;
		SoundCue->RecursiveFindAttenuation( SoundCue->FirstNode, AttenuationNodes );
		for (int32 NodeIndex = 0; NodeIndex < AttenuationNodes.Num(); ++NodeIndex)
		{
			AttenuationSettingsToApply = AttenuationNodes[NodeIndex]->GetAttenuationSettingsToApply();
			if (AttenuationSettingsToApply)
			{
				AttenuationSettingsToApply->CollectAttenuationShapesForVisualization(ShapeDetailsMap);
			}
		}
	}
}

void UAudioComponent::Activate(bool bReset)
{
	if (bReset || ShouldActivate() == true)
	{
		Play();
		if (IsActive())
		{
			OnComponentActivated.Broadcast(this, bReset);
		}
	}
}

void UAudioComponent::Deactivate()
{
	if (ShouldActivate() == false)
	{
		Stop();

		if (!IsActive())
		{
			OnComponentDeactivated.Broadcast(this);
		}
	}
}

void UAudioComponent::SetFloatParameter( const FName InName, const float InFloat )
{
	if (InName != NAME_None)
	{
		bool bFound = false;

		// First see if an entry for this name already exists
		for (FAudioComponentParam& P : InstanceParameters)
		{
			if (P.ParamName == InName)
			{
				P.FloatParam = InFloat;
				bFound = true;
				break;
			}
		}

		// We didn't find one, so create a new one.
		if (!bFound)
		{
			const int32 NewParamIndex = InstanceParameters.AddDefaulted();
			InstanceParameters[ NewParamIndex ].ParamName = InName;
			InstanceParameters[ NewParamIndex ].FloatParam = InFloat;
		}

		// If we're active we need to push this value to the ActiveSound
		if (IsActive())
		{
			if (FAudioDevice* AudioDevice = GetAudioDevice())
			{
				DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SetFloatParameter"), STAT_AudioSetFloatParameter, STATGROUP_AudioThreadCommands);

				const uint64 MyAudioComponentID = AudioComponentID;
				FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, InName, InFloat]()
				{
					FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID);
					if (ActiveSound)
					{
						ActiveSound->SetFloatParameter(InName, InFloat);
					}
				}, GET_STATID(STAT_AudioSetFloatParameter));
			}
		}
	}
}

void UAudioComponent::SetWaveParameter( const FName InName, USoundWave* InWave )
{
	if (InName != NAME_None)
	{
		bool bFound = false;
		// First see if an entry for this name already exists
		for (FAudioComponentParam& P : InstanceParameters)
		{
			if (P.ParamName == InName)
			{
				P.SoundWaveParam = InWave;
				bFound = true;
				break;
			}
		}

		// We didn't find one, so create a new one.
		if (!bFound)
		{
			const int32 NewParamIndex = InstanceParameters.AddDefaulted();
			InstanceParameters[NewParamIndex].ParamName = InName;
			InstanceParameters[NewParamIndex].SoundWaveParam = InWave;
		}

		// If we're active we need to push this value to the ActiveSound
		if (IsActive())
		{
			if (FAudioDevice* AudioDevice = GetAudioDevice())
			{
				DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SetWaveParameter"), STAT_AudioSetWaveParameter, STATGROUP_AudioThreadCommands);

				const uint64 MyAudioComponentID = AudioComponentID;
				FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, InName, InWave]()
				{
					FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID);
					if (ActiveSound)
					{
						ActiveSound->SetWaveParameter(InName, InWave);
					}
				}, GET_STATID(STAT_AudioSetWaveParameter));
			}
		}
	}
}

void UAudioComponent::SetBoolParameter( const FName InName, const bool InBool )
{
	if (InName != NAME_None)
	{
		bool bFound = false;
		// First see if an entry for this name already exists
		for (FAudioComponentParam& P : InstanceParameters)
		{
			if (P.ParamName == InName)
			{
				P.BoolParam = InBool;
				bFound = true;
				break;
			}
		}

		// We didn't find one, so create a new one.
		if (!bFound)
		{
			const int32 NewParamIndex = InstanceParameters.AddDefaulted();
			InstanceParameters[ NewParamIndex ].ParamName = InName;
			InstanceParameters[ NewParamIndex ].BoolParam = InBool;
		}

		// If we're active we need to push this value to the ActiveSound
		if (IsActive())
		{
			if (FAudioDevice* AudioDevice = GetAudioDevice())
			{
				DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SetBoolParameter"), STAT_AudioSetBoolParameter, STATGROUP_AudioThreadCommands);

				const uint64 MyAudioComponentID = AudioComponentID;
				FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, InName, InBool]()
				{
					FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID);
					if (ActiveSound)
					{
						ActiveSound->SetBoolParameter(InName, InBool);
					}
				}, GET_STATID(STAT_AudioSetBoolParameter));
			}
		}
	}
}

void UAudioComponent::SetIntParameter( const FName InName, const int32 InInt )
{
	if (InName != NAME_None)
	{
		bool bFound = false;
		// First see if an entry for this name already exists
		for (FAudioComponentParam& P : InstanceParameters)
		{
			if (P.ParamName == InName)
			{
				P.IntParam = InInt;
				bFound = true;
				break;
			}
		}

		// We didn't find one, so create a new one.
		if (!bFound)
		{
			const int32 NewParamIndex = InstanceParameters.AddDefaulted();
			InstanceParameters[NewParamIndex].ParamName = InName;
			InstanceParameters[NewParamIndex].IntParam = InInt;
		}

		// If we're active we need to push this value to the ActiveSound
		if (IsActive())
		{
			if (FAudioDevice* AudioDevice = GetAudioDevice())
			{
				DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SetIntParameter"), STAT_AudioSetIntParameter, STATGROUP_AudioThreadCommands);

				const uint64 MyAudioComponentID = AudioComponentID;
				FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, InName, InInt]()
				{
					FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID);
					if (ActiveSound)
					{
						ActiveSound->SetIntParameter(InName, InInt);
					}
				}, GET_STATID(STAT_AudioSetIntParameter));
			}
		}
	}
}

void UAudioComponent::SetSoundParameter(const FAudioComponentParam& Param)
{
	if (Param.ParamName != NAME_None)
	{
		bool bFound = false;
		// First see if an entry for this name already exists
		for (FAudioComponentParam& P : InstanceParameters)
		{
			if (P.ParamName == Param.ParamName)
			{
				P = Param;
				bFound = true;
				break;
			}
		}

		// We didn't find one, so create a new one.
		if (!bFound)
		{
			const int32 NewParamIndex = InstanceParameters.Add(Param);
		}

		if (IsActive())
		{
			if (FAudioDevice* AudioDevice = GetAudioDevice())
			{
				DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SetSoundParameter"), STAT_AudioSetSoundParameter, STATGROUP_AudioThreadCommands);

				const uint64 MyAudioComponentID = AudioComponentID;
				FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, Param]()
				{
					FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID);
					if (ActiveSound)
					{
						ActiveSound->SetSoundParameter(Param);
					}
				}, GET_STATID(STAT_AudioSetSoundParameter));
			}
		}
	}
}

void UAudioComponent::SetFadeInComplete()
{
	EAudioComponentPlayState PlayState = GetPlayState();
	if (PlayState != EAudioComponentPlayState::FadingIn)
	{
		BroadcastPlayState();
	}
}

void UAudioComponent::SetIsVirtualized(bool bInIsVirtualized)
{
	if (bIsVirtualized != bInIsVirtualized)
	{
		if (OnAudioVirtualizationChanged.IsBound())
		{
			OnAudioVirtualizationChanged.Broadcast(bInIsVirtualized);
		}

		if (OnAudioVirtualizationChangedNative.IsBound())
		{
			OnAudioVirtualizationChangedNative.Broadcast(this, bInIsVirtualized);
		}
	}

	bIsVirtualized = bInIsVirtualized ? 1 : 0;
}

void UAudioComponent::SetVolumeMultiplier(const float NewVolumeMultiplier)
{
	VolumeMultiplier = NewVolumeMultiplier;
	VolumeModulationMin = VolumeModulationMax = 1.f;

	if (IsActive())
	{
		if (FAudioDevice* AudioDevice = GetAudioDevice())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SetVolumeMultiplier"), STAT_AudioSetVolumeMultiplier, STATGROUP_AudioThreadCommands);

			const uint64 MyAudioComponentID = AudioComponentID;
			FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, NewVolumeMultiplier]()
			{
				FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID);
				if (ActiveSound)
				{
					ActiveSound->SetVolume(NewVolumeMultiplier);
				}
			}, GET_STATID(STAT_AudioSetVolumeMultiplier));
		}
	}
}

void UAudioComponent::SetPitchMultiplier(const float NewPitchMultiplier)
{
	PitchMultiplier = NewPitchMultiplier;
	PitchModulationMin = PitchModulationMax = 1.f;

	if (IsActive())
	{
		if (FAudioDevice* AudioDevice = GetAudioDevice())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SetPitchMultiplier"), STAT_AudioSetPitchMultiplier, STATGROUP_AudioThreadCommands);

			const uint64 MyAudioComponentID = AudioComponentID;
			FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, NewPitchMultiplier]()
			{
				FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID);
				if (ActiveSound)
				{
					ActiveSound->SetPitch(NewPitchMultiplier);
				}
			}, GET_STATID(STAT_AudioSetPitchMultiplier));
		}
	}
}

void UAudioComponent::SetUISound(const bool bInIsUISound)
{
	bIsUISound = bInIsUISound;

	if (IsActive())
	{
		if (FAudioDevice* AudioDevice = GetAudioDevice())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SetIsUISound"), STAT_AudioSetIsUISound, STATGROUP_AudioThreadCommands);

			const uint64 MyAudioComponentID = AudioComponentID;
			FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, bInIsUISound]()
			{
				FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID);
				if (ActiveSound)
				{
					ActiveSound->bIsUISound = bInIsUISound;
				}
			}, GET_STATID(STAT_AudioSetIsUISound));
		}
	}
}

void UAudioComponent::AdjustAttenuation(const FSoundAttenuationSettings& InAttenuationSettings)
{
	bOverrideAttenuation = true;
	AttenuationOverrides = InAttenuationSettings;

	if (IsActive())
	{
		if (FAudioDevice* AudioDevice = GetAudioDevice())
		{
			DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.AdjustAttenuation"), STAT_AudioAdjustAttenuation, STATGROUP_AudioThreadCommands);

			const uint64 MyAudioComponentID = AudioComponentID;
			FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, InAttenuationSettings]()
			{
				FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID);
				if (ActiveSound)
				{
					ActiveSound->AttenuationSettings = InAttenuationSettings;
				}
			}, GET_STATID(STAT_AudioAdjustAttenuation));
		}
	}
}

void UAudioComponent::SetSubmixSend(USoundSubmixBase* Submix, float SendLevel)
{
	if (FAudioDevice* AudioDevice = GetAudioDevice())
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.AudioSetSubmixSend"), STAT_SetSubmixSend, STATGROUP_AudioThreadCommands);

		const uint64 MyAudioComponentID = AudioComponentID;
		FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, Submix, SendLevel]()
		{
			FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID);
			if (ActiveSound)
			{
				FSoundSubmixSendInfo SendInfo;
				SendInfo.SoundSubmix = Submix;
				SendInfo.SendLevel = SendLevel;
				ActiveSound->SetSubmixSend(SendInfo);
			}
		}, GET_STATID(STAT_SetSubmixSend));
	}
}

void UAudioComponent::SetBusSendffectInternal(USoundSourceBus* InSourceBus, UAudioBus* InAudioBus, float SendLevel, EBusSendType InBusSendType)
{
	if (FAudioDevice* AudioDevice = GetAudioDevice())
	{
		const uint64 MyAudioComponentID = AudioComponentID;
		FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, InSourceBus, InAudioBus, SendLevel, InBusSendType]()
		{
			FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID);
			if (ActiveSound)
			{
				FSoundSourceBusSendInfo SourceBusSendInfo;
				SourceBusSendInfo.SoundSourceBus = InSourceBus;
				SourceBusSendInfo.AudioBus = InAudioBus;
				SourceBusSendInfo.SendLevel = SendLevel;

				ActiveSound->SetSourceBusSend(InBusSendType, SourceBusSendInfo);
			}
		});
	}
}

void UAudioComponent::SetSourceBusSendPreEffect(USoundSourceBus* SoundSourceBus, float SourceBusSendLevel)
{
	SetBusSendffectInternal(SoundSourceBus, nullptr, SourceBusSendLevel, EBusSendType::PreEffect);
}

void UAudioComponent::SetSourceBusSendPostEffect(USoundSourceBus * SoundSourceBus, float SourceBusSendLevel)
{
	SetBusSendffectInternal(SoundSourceBus, nullptr, SourceBusSendLevel, EBusSendType::PostEffect);
}

void UAudioComponent::SetAudioBusSendPreEffect(UAudioBus* AudioBus, float AudioBusSendLevel)
{
	SetBusSendffectInternal(nullptr, AudioBus, AudioBusSendLevel, EBusSendType::PreEffect);
}

void UAudioComponent::SetAudioBusSendPostEffect(UAudioBus* AudioBus, float AudioBusSendLevel)
{
	SetBusSendffectInternal(nullptr, AudioBus, AudioBusSendLevel, EBusSendType::PostEffect);
}

void UAudioComponent::SetLowPassFilterEnabled(bool InLowPassFilterEnabled)
{
	if (FAudioDevice* AudioDevice = GetAudioDevice())
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SetLowPassFilterFrequency"), STAT_AudioSetLowPassFilterEnabled, STATGROUP_AudioThreadCommands);

		const uint64 MyAudioComponentID = AudioComponentID;
		FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, InLowPassFilterEnabled]()
		{
			FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID);
			if (ActiveSound)
			{
				ActiveSound->bEnableLowPassFilter = InLowPassFilterEnabled;
			}
		}, GET_STATID(STAT_AudioSetLowPassFilterEnabled));
	}
}

void UAudioComponent::SetLowPassFilterFrequency(float InLowPassFilterFrequency)
{
	if (FAudioDevice* AudioDevice = GetAudioDevice())
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SetLowPassFilterFrequency"), STAT_AudioSetLowPassFilterFrequency, STATGROUP_AudioThreadCommands);

		const uint64 MyAudioComponentID = AudioComponentID;
		FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, InLowPassFilterFrequency]()
		{
			FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID);
			if (ActiveSound)
			{
				ActiveSound->LowPassFilterFrequency = InLowPassFilterFrequency;
			}
		}, GET_STATID(STAT_AudioSetLowPassFilterFrequency));
	}
}

void UAudioComponent::SetOutputToBusOnly(bool bInOutputToBusOnly)
{
	if (FAudioDevice* AudioDevice = GetAudioDevice())
	{
		DECLARE_CYCLE_STAT(TEXT("FAudioThreadTask.SetOutputToBusOnly"), STAT_AudioSetOutputToBusOnly, STATGROUP_AudioThreadCommands);

		const uint64 MyAudioComponentID = AudioComponentID;
		FAudioThread::RunCommandOnAudioThread([AudioDevice, MyAudioComponentID, bInOutputToBusOnly]()
		{
			FActiveSound* ActiveSound = AudioDevice->FindActiveSound(MyAudioComponentID);
			if (ActiveSound)
			{
				ActiveSound->bHasActiveMainSubmixOutputOverride = true;
				ActiveSound->bHasActiveSubmixSendRoutingOverride = true;
				if (bInOutputToBusOnly)
				{
					ActiveSound->bHasActiveBusSendRoutingOverride = true;
					ActiveSound->bEnableBusSendRoutingOverride = true;
				}
				ActiveSound->bEnableMainSubmixOutputOverride = !bInOutputToBusOnly;
				ActiveSound->bEnableSubmixSendRoutingOverride = !bInOutputToBusOnly;
			}
		}, GET_STATID(STAT_AudioSetOutputToBusOnly));
	}
}

bool UAudioComponent::HasCookedFFTData() const
{
	if (Sound)
	{
		return Sound->HasCookedFFTData();
	}
	return false;
}

bool UAudioComponent::HasCookedAmplitudeEnvelopeData() const
{
	if (Sound)
	{
		return Sound->HasCookedAmplitudeEnvelopeData();
	}
	return false;
}

void UAudioComponent::SetPlaybackTimes(const TMap<uint32, float>& InSoundWavePlaybackTimes)
{
	// Reset the playback times for everything in case the wave instance stops and is not updated
	for (auto& Elem : SoundWavePlaybackTimes)
	{
		Elem.Value.PlaybackTime = 0.0f;
	}

	for (auto& Elem : InSoundWavePlaybackTimes)
	{
		uint32 ObjectId = Elem.Key;
		FSoundWavePlaybackTimeData* PlaybackTimeData = SoundWavePlaybackTimes.Find(ObjectId);
		if (PlaybackTimeData)
		{
			PlaybackTimeData->PlaybackTime = FMath::Max(Elem.Value - BakedAnalysisTimeShiftCVar, 0.0f);
		}
	}
}

bool UAudioComponent::GetCookedFFTData(const TArray<float>& FrequenciesToGet, TArray<FSoundWaveSpectralData>& OutSoundWaveSpectralData)
{
	bool bHadData = false;
	if (IsPlaying() && SoundWavePlaybackTimes.Num() > 0 && FrequenciesToGet.Num() > 0)
	{
		OutSoundWaveSpectralData.Reset();
		for (float Frequency : FrequenciesToGet)
		{
			FSoundWaveSpectralData NewEntry;
			NewEntry.FrequencyHz = Frequency;
			OutSoundWaveSpectralData.Add(NewEntry);
		}

		// Sort by frequency (lowest frequency first).
		OutSoundWaveSpectralData.Sort(FCompareSpectralDataByFrequencyHz());

		int32 NumEntriesAdded = 0;
		for (auto& Entry : SoundWavePlaybackTimes)
		{
			if (Entry.Value.PlaybackTime > 0.0f && Entry.Value.SoundWave->CookedSpectralTimeData.Num() > 0)
			{
				static TArray<FSoundWaveSpectralData> CookedSpectralData;
				CookedSpectralData.Reset();

				// Find the point in the spectral data that corresponds to the time
				Entry.Value.SoundWave->GetInterpolatedCookedFFTDataForTime(Entry.Value.PlaybackTime, Entry.Value.LastFFTCookedIndex, CookedSpectralData, Sound->IsLooping());

				if (CookedSpectralData.Num() > 0)
				{
					// Find the interpolated values given the frequencies we want to get
					for (FSoundWaveSpectralData& OutSpectralData : OutSoundWaveSpectralData)
					{
						// Check min edge case: we're requesting cooked FFT data lower than what we have cooked
						if (OutSpectralData.FrequencyHz < CookedSpectralData[0].FrequencyHz)
						{
							// Just mix in the lowest value we have cooked
							OutSpectralData.Magnitude += CookedSpectralData[0].Magnitude;
							OutSpectralData.NormalizedMagnitude += CookedSpectralData[0].NormalizedMagnitude;
						}
						// Check max edge case: we're requesting cooked FFT data at a higher frequency than what we have cooked
						else if (OutSpectralData.FrequencyHz >= CookedSpectralData.Last().FrequencyHz)
						{
							// Just mix in the highest value we have cooked
							OutSpectralData.Magnitude += CookedSpectralData.Last().Magnitude;
							OutSpectralData.NormalizedMagnitude += CookedSpectralData.Last().NormalizedMagnitude;
						}
						// We need to find the 2 closest cooked results and interpolate those
						else
						{
							for (int32 SpectralDataIndex = 0; SpectralDataIndex < CookedSpectralData.Num() - 1; ++SpectralDataIndex)
							{
								const FSoundWaveSpectralData& CurrentSpectralData = CookedSpectralData[SpectralDataIndex];
								const FSoundWaveSpectralData& NextSpectralData = CookedSpectralData[SpectralDataIndex + 1];
								if (OutSpectralData.FrequencyHz >= CurrentSpectralData.FrequencyHz && OutSpectralData.FrequencyHz < NextSpectralData.FrequencyHz)
								{
									float Alpha = (OutSpectralData.FrequencyHz - CurrentSpectralData.FrequencyHz) / (NextSpectralData.FrequencyHz - CurrentSpectralData.FrequencyHz);
									OutSpectralData.Magnitude += FMath::Lerp(CurrentSpectralData.Magnitude, NextSpectralData.Magnitude, Alpha);
									OutSpectralData.NormalizedMagnitude += FMath::Lerp(CurrentSpectralData.NormalizedMagnitude, NextSpectralData.NormalizedMagnitude, Alpha);

									break;
								}
							}
						}
					}

					++NumEntriesAdded;
					bHadData = true;
				}
			}
		}

		// Divide by the number of entries we added (i.e. we are averaging together multiple cooked FFT data in the case of multiple sound waves playing with cooked data)
		if (NumEntriesAdded > 1)
		{
			for (FSoundWaveSpectralData& OutSpectralData : OutSoundWaveSpectralData)
			{
				OutSpectralData.Magnitude /= NumEntriesAdded;
				OutSpectralData.NormalizedMagnitude /= NumEntriesAdded;
			}
		}
	}

	return bHadData;
}

bool UAudioComponent::GetCookedFFTDataForAllPlayingSounds(TArray<FSoundWaveSpectralDataPerSound>& OutSoundWaveSpectralData)
{
	bool bHadData = false;
	if (IsPlaying() && SoundWavePlaybackTimes.Num() > 0)
	{
		OutSoundWaveSpectralData.Reset();

		for (auto& Entry : SoundWavePlaybackTimes)
		{
			if (Entry.Value.PlaybackTime > 0.0f && Entry.Value.SoundWave->CookedSpectralTimeData.Num() > 0)
			{
				FSoundWaveSpectralDataPerSound NewOutput;
				NewOutput.SoundWave = Entry.Value.SoundWave;
				NewOutput.PlaybackTime = Entry.Value.PlaybackTime;

				// Find the point in the spectral data that corresponds to the time
				Entry.Value.SoundWave->GetInterpolatedCookedFFTDataForTime(Entry.Value.PlaybackTime, Entry.Value.LastFFTCookedIndex, NewOutput.SpectralData, Sound->IsLooping());
				if (NewOutput.SpectralData.Num())
				{
					OutSoundWaveSpectralData.Add(NewOutput);
					bHadData = true;
				}
			}
		}
	}
	return bHadData;
}

bool UAudioComponent::GetCookedEnvelopeData(float& OutEnvelopeData)
{
	bool bHadData = false;
	if (IsPlaying() && SoundWavePlaybackTimes.Num() > 0)
	{
		static TArray<FSoundWaveEnvelopeTimeData> CookedEnvelopeData;
		int32 NumEntriesAdded = 0;
		OutEnvelopeData = 0.0f;
		for (auto& Entry : SoundWavePlaybackTimes)
		{
			if (Entry.Value.SoundWave->CookedEnvelopeTimeData.Num() > 0 && Entry.Value.PlaybackTime > 0.0f)
			{
				CookedEnvelopeData.Reset();

				// Find the point in the spectral data that corresponds to the time
				float SoundWaveAmplitude = 0.0f;
				if (Entry.Value.SoundWave->GetInterpolatedCookedEnvelopeDataForTime(Entry.Value.PlaybackTime, Entry.Value.LastEnvelopeCookedIndex, SoundWaveAmplitude, Sound->IsLooping()))
				{
					OutEnvelopeData += SoundWaveAmplitude;
					++NumEntriesAdded;
					bHadData = true;
				}
			}
		}

		// Divide by number of entries we added... get average amplitude envelope
		if (bHadData)
		{
			OutEnvelopeData /= NumEntriesAdded;
		}
	}

	return bHadData;
}

bool UAudioComponent::GetCookedEnvelopeDataForAllPlayingSounds(TArray<FSoundWaveEnvelopeDataPerSound>& OutEnvelopeData)
{
	bool bHadData = false;
	if (IsPlaying() && SoundWavePlaybackTimes.Num() > 0)
	{
		for (auto& Entry : SoundWavePlaybackTimes)
		{
			if (Entry.Value.SoundWave->CookedEnvelopeTimeData.Num() > 0 && Entry.Value.PlaybackTime > 0.0f)
			{
				// Find the point in the spectral data that corresponds to the time
				float SoundWaveAmplitude = 0.0f;
				if (Entry.Value.SoundWave->GetInterpolatedCookedEnvelopeDataForTime(Entry.Value.PlaybackTime, Entry.Value.LastEnvelopeCookedIndex, SoundWaveAmplitude, Sound->IsLooping()))
				{
					FSoundWaveEnvelopeDataPerSound NewOutput;
					NewOutput.SoundWave = Entry.Value.SoundWave;
					NewOutput.PlaybackTime = Entry.Value.PlaybackTime;
					NewOutput.Envelope = SoundWaveAmplitude;
					OutEnvelopeData.Add(NewOutput);
					bHadData = true;
				}

			}
		}
	}
	return bHadData;
}

void UAudioComponent::SetSourceEffectChain(USoundEffectSourcePresetChain* InSourceEffectChain)
{
	SourceEffectChain = InSourceEffectChain;
}