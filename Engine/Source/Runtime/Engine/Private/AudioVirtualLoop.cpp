// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "AudioVirtualLoop.h"

#include "ActiveSound.h"
#include "Audio/AudioDebug.h"
#include "AudioDevice.h"
#include "Sound/SoundBase.h"


static int32 bVirtualLoopsEnabledCVar = 1;
FAutoConsoleVariableRef CVarVirtualLoopsEnabled(
	TEXT("au.VirtualLoops.Enabled"),
	bVirtualLoopsEnabledCVar,
	TEXT("Enables or disables whether virtualizing is supported for audio loops.\n"),
	ECVF_Default);

static float VirtualLoopsPerfDistanceCVar = 15000.0f;
FAutoConsoleVariableRef CVarVirtualLoopsPerfDistance(
	TEXT("au.VirtualLoops.PerfDistance"),
	VirtualLoopsPerfDistanceCVar,
	TEXT("Sets virtual loop distance to scale update rate between min and max beyond max audible distance of sound.\n"),
	ECVF_Default);

static float VirtualLoopsUpdateRateMinCVar = 0.1f;
FAutoConsoleVariableRef CVarVirtualLoopsUpdateRateMin(
	TEXT("au.VirtualLoops.UpdateRate.Min"),
	VirtualLoopsUpdateRateMinCVar,
	TEXT("Sets minimum rate to check if sound becomes audible again at sound's max audible distance.\n"),
	ECVF_Default);

static float VirtualLoopsUpdateRateMaxCVar = 3.0f;
FAutoConsoleVariableRef CVarVirtualLoopsUpdateRateMax(
	TEXT("au.VirtualLoops.UpdateRate.Max"),
	VirtualLoopsUpdateRateMaxCVar,
	TEXT("Sets maximum rate to check if sound becomes audible again (at beyond sound's max audible distance + perf scaling distance).\n"),
	ECVF_Default);


FAudioVirtualLoop::FAudioVirtualLoop()
	: TimeSinceLastUpdate(0.0f)
	, UpdateInterval(0.0f)
	, ActiveSound(nullptr)
{
}

bool FAudioVirtualLoop::Virtualize(const FActiveSound& InActiveSound, bool bDoRangeCheck, FAudioVirtualLoop& OutVirtualLoop)
{
	FAudioDevice* AudioDevice = InActiveSound.AudioDevice;
	check(AudioDevice);

	return Virtualize(InActiveSound, *AudioDevice, bDoRangeCheck, OutVirtualLoop);
}

bool FAudioVirtualLoop::Virtualize(const FActiveSound& InActiveSound, FAudioDevice& InAudioDevice, bool bDoRangeCheck, FAudioVirtualLoop& OutVirtualLoop)
{
	USoundBase* Sound = InActiveSound.GetSound();
	check(Sound);

	if (Sound->VirtualizationMode == EVirtualizationMode::Disabled)
	{
		return false;
	}

	if (!bVirtualLoopsEnabledCVar || InActiveSound.bIsPreviewSound || !InActiveSound.IsLooping())
	{
		return false;
	}

	if (InActiveSound.bFadingOut || InActiveSound.bIsStopping)
	{
		return false;
	}

	if (bDoRangeCheck && IsInAudibleRange(InActiveSound, &InAudioDevice))
	{
		return false;
	}

	FActiveSound* ActiveSound = FActiveSound::CreateVirtualCopy(InActiveSound, InAudioDevice);
	OutVirtualLoop.ActiveSound = ActiveSound;
	return true;
}

void FAudioVirtualLoop::CalculateUpdateInterval(bool bIsAtMaxConcurrency)
{
	// If calculating due to being at max concurrency, set to max rate as
	// sound will most likely be killed again on next check until concurrency
	// is no longer full.  This limits starting and stopping of excess sounds
	// virtualizing.
	if (bIsAtMaxConcurrency)
	{
		UpdateInterval = VirtualLoopsUpdateRateMaxCVar;
	}
	else
	{
		check(ActiveSound);
		FAudioDevice* AudioDevice = ActiveSound->AudioDevice;
		check(AudioDevice);

		const float DistanceToListener = AudioDevice->GetDistanceToNearestListener(ActiveSound->Transform.GetLocation());
		const float DistanceRatio = (DistanceToListener - ActiveSound->MaxDistance) / FMath::Max(VirtualLoopsPerfDistanceCVar, 1.0f);
		const float DistanceRatioClamped = FMath::Clamp(DistanceRatio, 0.0f, 1.0f);
		UpdateInterval = FMath::Lerp(VirtualLoopsUpdateRateMinCVar, VirtualLoopsUpdateRateMaxCVar, DistanceRatioClamped);
	}
}

FActiveSound& FAudioVirtualLoop::GetActiveSound()
{
	check(ActiveSound);
	return *ActiveSound;
}

const FActiveSound& FAudioVirtualLoop::GetActiveSound() const
{
	check(ActiveSound);
	return *ActiveSound;
}

bool FAudioVirtualLoop::IsEnabled()
{
	return bVirtualLoopsEnabledCVar != 0;
}

bool FAudioVirtualLoop::IsInAudibleRange(const FActiveSound& InActiveSound, const FAudioDevice* InAudioDevice)
{
	if (!InActiveSound.bAllowSpatialization)
	{
		return true;
	}

	const FAudioDevice* AudioDevice = InAudioDevice;
	if (!AudioDevice)
	{
		AudioDevice = InActiveSound.AudioDevice;
	}
	check(AudioDevice);

	if (InActiveSound.IsPlayWhenSilent())
	{
		return true;
	}

	float DistanceScale = 1.0f;
	if (InActiveSound.bHasAttenuationSettings)
	{
		// If we are not using distance-based attenuation, this sound will be audible regardless of distance.
		const FSoundAttenuationSettings* AttenuationSettingsToApply = InActiveSound.bHasAttenuationSettings ? &InActiveSound.AttenuationSettings : nullptr;
		if (!AttenuationSettingsToApply->bAttenuate)
		{
			return true;
		}

		DistanceScale = AttenuationSettingsToApply->GetFocusDistanceScale(AudioDevice->GetGlobalFocusSettings(), InActiveSound.FocusDistanceScale);
	}

	DistanceScale = FMath::Max(DistanceScale, 0.0001f);
	const FVector Location = InActiveSound.Transform.GetLocation();
	return AudioDevice->LocationIsAudible(Location, InActiveSound.MaxDistance / DistanceScale);
}

bool FAudioVirtualLoop::CanRealize(float DeltaTime)
{
	if (UpdateInterval > 0.0f)
	{
		TimeSinceLastUpdate += DeltaTime;
		if (UpdateInterval > TimeSinceLastUpdate)
		{
			return false;
		}
		TimeSinceLastUpdate = 0.0f;
	}

#if ENABLE_AUDIO_DEBUG
	FAudioDebugger::DrawDebugInfo(*this);
#endif // ENABLE_AUDIO_DEBUG

	// If not audible, update when will be checked again and return false
	if (!IsInAudibleRange(*ActiveSound))
	{
		CalculateUpdateInterval();
		return false;
	}

	return true;
}
