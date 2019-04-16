// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "AudioVirtualLoop.h"

#include "ActiveSound.h"
#include "AudioDevice.h"
#include "Sound/SoundBase.h"

	static int32 bVirtualLoopsEnabledCVar = 1;
	FAutoConsoleVariableRef CVarVirtualLoopsEnabled(
		TEXT("au.VirtualLoopsEnabled"),
		bVirtualLoopsEnabledCVar,
		TEXT("Enables or disables whether virtualizing is supported for audio loops.\n"),
		ECVF_Default);

	static float VirtualLoopsPerfDistanceCVar = 15000.0f;
	FAutoConsoleVariableRef CVarVirtualLoopsPerfDistance(
		TEXT("au.VirtualLoopsPerfDistance"),
		VirtualLoopsPerfDistanceCVar,
		TEXT("Sets virtual loop distance to scale update rate between min and max beyond max audible distance of sound.\n"),
		ECVF_Default);

	static float VirtualUpdateRateMinCVar = 0.1f;
	FAutoConsoleVariableRef CVarVirtualLoopsUpdateRateMin(
		TEXT("au.VirtualLoopsUpdateRateMin"),
		VirtualUpdateRateMinCVar,
		TEXT("Sets minimum rate to check if sound becomes audible again at sound's max audible distance.\n"),
		ECVF_Default);

	static float VirtualUpdateRateMaxCVar = 3.0f;
	FAutoConsoleVariableRef CVarVirtualLoopsUpdateRateMax(
		TEXT("au.VirtualLoopsUpdateRateMax"),
		VirtualUpdateRateMinCVar,
		TEXT("Sets maximum rate to check if sound becomes audible again (at beyond sound's max audible distance + perf scaling distance).\n"),
		ECVF_Default);

FAudioVirtualLoop::FAudioVirtualLoop(FAudioDevice& InAudioDevice, const FActiveSound& NewActiveSound)
	: TimeSinceLastUpdate(0.0f)
	, UpdateInterval(0.0f)
	, AudioDevice(&InAudioDevice)
	, ActiveSound(nullptr)
{
	SetActiveSound(NewActiveSound);
}

FAudioVirtualLoop::~FAudioVirtualLoop()
{
	check(ActiveSound);
	delete(ActiveSound);
}

FAudioVirtualLoop* FAudioVirtualLoop::Virtualize(FAudioDevice& InAudioDevice, const FActiveSound& InActiveSound, bool bDoRangeCheck)
{
	if (!bVirtualLoopsEnabledCVar || InActiveSound.bIsPreviewSound || !InActiveSound.IsLooping())
	{
		return nullptr;
	}

	if (bDoRangeCheck && IsInAudibleRange(InAudioDevice, InActiveSound))
	{
		return nullptr;
	}

	return new FAudioVirtualLoop(InAudioDevice, InActiveSound);
}

void FAudioVirtualLoop::CalculateUpdateInterval(bool bIsAtMaxConcurrency)
{
	// If calculating due to being at max concurrency, set to max rate as
	// sound will most likely be killed again on next check until concurrency
	// is no longer full.  This limits starting and stopping of excess sounds
	// virtualizing.
	if (bIsAtMaxConcurrency)
	{
		UpdateInterval = VirtualUpdateRateMaxCVar;
	}
	else
	{
		const float DistanceToListener = AudioDevice->GetDistanceToNearestListener(ActiveSound->Transform.GetLocation());
		const float DistanceRatio = (DistanceToListener - ActiveSound->MaxDistance) / FMath::Max(VirtualLoopsPerfDistanceCVar, 1.0f);
		const float DistanceRatioClamped = FMath::Clamp(DistanceRatio, 0.0f, 1.0f);
		UpdateInterval = FMath::Lerp(VirtualUpdateRateMinCVar, VirtualUpdateRateMaxCVar, DistanceRatioClamped);
	}
}

FActiveSound& FAudioVirtualLoop::GetActiveSound()
{
	check(ActiveSound);
	return *ActiveSound;
}

bool FAudioVirtualLoop::IsInAudibleRange(const FAudioDevice& InAudioDevice, const FActiveSound& InActiveSound)
{
	const FSoundAttenuationSettings* AttenuationSettingsToApply = InActiveSound.bHasAttenuationSettings ? &InActiveSound.AttenuationSettings : nullptr;

	if (!InActiveSound.bAllowSpatialization)
	{
		return true;
	}

	float DistanceScale = 1.0f;
	if (InActiveSound.bHasAttenuationSettings)
	{
		// If we are not using distance-based attenuation, this sound will be audible regardless of distance.
		if (!AttenuationSettingsToApply->bAttenuate)
		{
			return true;
		}

		DistanceScale = AttenuationSettingsToApply->GetFocusDistanceScale(InAudioDevice.GetGlobalFocusSettings(), InActiveSound.FocusDistanceScale);
	}

	DistanceScale = FMath::Max(DistanceScale, 0.0001f);
	const FVector Location = InActiveSound.Transform.GetLocation();
	return InAudioDevice.LocationIsAudible(Location, InActiveSound.MaxDistance / DistanceScale);
}

void FAudioVirtualLoop::SetActiveSound(const FActiveSound& InActiveSound)
{
	check(!ActiveSound);

	ActiveSound = new FActiveSound();

	ActiveSound->bIsUISound = InActiveSound.bIsUISound;

	ActiveSound->SetAudioComponent(InActiveSound);
	ActiveSound->SetAudioDevice(AudioDevice);

	if (GIsEditor)
	{
		ActiveSound->SetWorld(InActiveSound.GetWorld());
	}

	ActiveSound->SetSound(InActiveSound.GetSound());
	ActiveSound->SetSoundClass(InActiveSound.GetSoundClass());

	ActiveSound->ConcurrencySet = InActiveSound.ConcurrencySet;
	ActiveSound->VolumeMultiplier = InActiveSound.VolumeMultiplier;
	ActiveSound->Priority = InActiveSound.Priority;
	ActiveSound->PitchMultiplier = InActiveSound.PitchMultiplier;
	ActiveSound->bEnableLowPassFilter = InActiveSound.bEnableLowPassFilter;
	ActiveSound->LowPassFilterFrequency = InActiveSound.LowPassFilterFrequency;
	ActiveSound->RequestedStartTime = InActiveSound.RequestedStartTime;
	ActiveSound->SubtitlePriority = InActiveSound.SubtitlePriority;
	ActiveSound->bShouldRemainActiveIfDropped = InActiveSound.bShouldRemainActiveIfDropped;
	ActiveSound->bHandleSubtitles = InActiveSound.bHandleSubtitles;
	ActiveSound->bIgnoreForFlushing = InActiveSound.bIgnoreForFlushing;
	ActiveSound->bIsUISound = InActiveSound.bIsUISound;
	ActiveSound->bIsMusic = InActiveSound.bIsMusic;
	ActiveSound->bAlwaysPlay = InActiveSound.bAlwaysPlay;
	ActiveSound->bReverb = InActiveSound.bReverb;
	ActiveSound->bCenterChannelOnly = InActiveSound.bCenterChannelOnly;
	ActiveSound->bIsPreviewSound = InActiveSound.bIsPreviewSound;
	ActiveSound->bLocationDefined = InActiveSound.bLocationDefined;
	ActiveSound->bIsPaused = InActiveSound.bIsPaused;
	ActiveSound->Transform = InActiveSound.Transform;
	ActiveSound->bAllowSpatialization = InActiveSound.bAllowSpatialization;
	ActiveSound->bHasAttenuationSettings = InActiveSound.bHasAttenuationSettings;
	ActiveSound->AttenuationSettings = InActiveSound.AttenuationSettings;
	ActiveSound->FocusPriorityScale = InActiveSound.FocusPriorityScale;
	ActiveSound->FocusDistanceScale = InActiveSound.FocusDistanceScale;
	ActiveSound->EnvelopeFollowerAttackTime = InActiveSound.EnvelopeFollowerAttackTime;
	ActiveSound->EnvelopeFollowerReleaseTime = InActiveSound.EnvelopeFollowerReleaseTime;

	ActiveSound->bUpdatePlayPercentage = InActiveSound.bUpdatePlayPercentage;
	ActiveSound->bUpdateSingleEnvelopeValue = InActiveSound.bUpdateSingleEnvelopeValue;
	ActiveSound->bUpdateMultiEnvelopeValue = InActiveSound.bUpdateMultiEnvelopeValue;

	ActiveSound->bUpdatePlaybackTime = InActiveSound.bUpdatePlaybackTime;

	ActiveSound->MaxDistance = InActiveSound.MaxDistance;
	ActiveSound->InstanceParameters = InActiveSound.InstanceParameters;
	ActiveSound->TargetAdjustVolumeMultiplier = InActiveSound.TargetAdjustVolumeMultiplier;
	ActiveSound->CurrentAdjustVolumeMultiplier = InActiveSound.CurrentAdjustVolumeMultiplier;
	ActiveSound->TargetAdjustVolumeStopTime = InActiveSound.TargetAdjustVolumeStopTime;
}

void FAudioVirtualLoop::Update()
{
	check(AudioDevice);
	check(ActiveSound);

	const float DeltaTime = AudioDevice->GetDeviceDeltaTime();

	// Keep playback time up-to-date as it may be used to evaluate concurrency
	ActiveSound->PlaybackTime += DeltaTime * ActiveSound->MinCurrentPitch;

	if (UpdateInterval > 0.0f)
	{
		TimeSinceLastUpdate += DeltaTime;
		if (UpdateInterval > TimeSinceLastUpdate)
		{
			return;
		}
		TimeSinceLastUpdate = 0.0f;
	}

	if (IsInAudibleRange(*AudioDevice, *ActiveSound))
	{
		AudioDevice->RetriggerVirtualLoop(*this);
	}
	else
	{
		CalculateUpdateInterval();
	}
}
