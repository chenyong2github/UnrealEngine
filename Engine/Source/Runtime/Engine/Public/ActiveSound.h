// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorldCollision.h"
#include "Sound/SoundAttenuation.h"
#include "HAL/ThreadSafeBool.h"
#include "Audio.h"
#include "Audio/AudioDebug.h"
#include "AudioDynamicParameter.h"
#include "Components/AudioComponent.h"
#include "DSP/VolumeFader.h"
#include "IAudioExtensionPlugin.h"
#include "Sound/AudioVolume.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundSourceBus.h"

class FAudioDevice;
class USoundBase;
class USoundSubmix;
class USoundSourceBus;
struct FSoundSubmixSendInfo;
struct FSoundSourceBusSendInfo;
struct FWaveInstance;
class USoundWave;
struct FListener;
struct FAttenuationListenerData;

/**
 * Attenuation focus system data computed per update per active sound
 */
struct FAttenuationFocusData
{
	/** Azimuth of the active sound relative to the listener. Used by sound focus. */
	float Azimuth;

	/** Absolute azimuth of the active sound relative to the listener. Used for 3d audio calculations. */
	float AbsoluteAzimuth;

	/** Value used to allow smooth interpolation in/out of focus */
	float FocusFactor;

	/** Cached calculation of the amount distance is scaled due to focus */
	float DistanceScale;

	/** The amount priority is scaled due to focus */
	float PriorityScale;

	/** Cached highest priority of the parent active sound's wave instances. */
	float PriorityHighest;

	/** The amount volume is scaled due to focus */
	float VolumeScale;

	FAttenuationFocusData()
		: Azimuth(0.0f)
		, AbsoluteAzimuth(0.0f)
		, FocusFactor(1.0f)
		, DistanceScale(1.0f)
		, PriorityScale(1.0f)
		, PriorityHighest(1.0f)
		, VolumeScale(1.0f)
	{
	}
};

/**
 *	Struct used for gathering the final parameters to apply to a wave instance
 */
struct FSoundParseParameters
{
	// A collection of finish notification hooks
	FNotifyBufferFinishedHooks NotifyBufferFinishedHooks;

	// The Sound Class to use the settings of
	USoundClass* SoundClass;

	// The transform of the sound (scale is not used)
	FTransform Transform;

	// The speed that the sound is moving relative to the listener
	FVector Velocity;

	// The volume product of the sound
	float Volume;

	// The attenuation of the sound due to distance attenuation
	float DistanceAttenuation;

	// A volume scale on the sound specified by user
	float VolumeMultiplier;


	// Attack time of the source envelope follower
	int32 EnvelopeFollowerAttackTime;

	// Release time of the source envelope follower
	int32 EnvelopeFollowerReleaseTime;

	// The multiplier to apply if the sound class desires
	float InteriorVolumeMultiplier;

	// The priority of sound, which is the product of the component priority and the USoundBased priority
	float Priority;

	// The pitch scale factor of the sound
	float Pitch;

	// Time offset from beginning of sound to start at
	float StartTime;

	// At what distance from the source of the sound should spatialization begin
	float OmniRadius;

	// The distance over which the sound is attenuated
	float AttenuationDistance;

	// The distance from the listener to the sound
	float ListenerToSoundDistance;

	// The distance from the listener to the sound (ignores attenuation settings)
	float ListenerToSoundDistanceForPanning;

	// The absolute azimuth angle of the sound relative to the forward listener vector (359 degrees to left, 1 degrees to right)
	float AbsoluteAzimuth;

	// The sound submix to use for the wave instance
	USoundSubmix* SoundSubmix;

	// The submix sends to use
	TArray<FSoundSubmixSendInfo> SoundSubmixSends;

	// The source bus sends to use
	TArray<FSoundSourceBusSendInfo> SoundSourceBusSends[(int32)EBusSendType::Count];

	// Reverb wet-level parameters
	EReverbSendMethod ReverbSendMethod;
	FVector2D ReverbSendLevelRange;
	FVector2D ReverbSendLevelDistanceRange;
	float ManualReverbSendLevel;
	FRuntimeFloatCurve CustomReverbSendCurve;

	// The distance between left and right channels when spatializing stereo assets
	float StereoSpread;

	// Which spatialization algorithm to use
	ESoundSpatializationAlgorithm SpatializationMethod;

	// What occlusion plugin source settings to use
	USpatializationPluginSourceSettingsBase* SpatializationPluginSettings;

	// What occlusion plugin source settings to use
	UOcclusionPluginSourceSettingsBase* OcclusionPluginSettings;

	/** Instance of modulation source settings to use. */
	USoundModulationPluginSourceSettingsBase* ModulationPluginSettings;

	// What reverb plugin source settings to use
	UReverbPluginSourceSettingsBase* ReverbPluginSettings;

	// What source effect chain to use
	USoundEffectSourcePresetChain* SourceEffectChain;

	// The lowpass filter frequency to apply (if enabled)
	float LowPassFilterFrequency;

	// The lowpass filter frequency to apply due to distance attenuation
	float AttenuationLowpassFilterFrequency;

	// The highpass filter frequency to apply due to distance attenuation
	float AttenuationHighpassFilterFrequency;

	// The lowpass filter to apply if the sound is occluded
	float OcclusionFilterFrequency;

	// The lowpass filter to apply if the sound is inside an ambient zone
	float AmbientZoneFilterFrequency;

	// Whether or not to output this audio to buses only
	uint8 bOutputToBusOnly:1;

	// Whether the sound should be spatialized
	uint8 bUseSpatialization:1;

	// Whether the sound should be seamlessly looped
	uint8 bLooping:1;

	// Whether we have enabled low-pass filtering of this sound
	uint8 bEnableLowPassFilter:1;

	// Whether this sound is occluded
	uint8 bIsOccluded:1;

	// Whether or not this sound is manually paused (i.e. not by application-wide pause)
	uint8 bIsPaused:1;

	// Whether or not this sound can re-trigger
	uint8 bEnableRetrigger : 1;

	// Whether or not to apply a =6 dB attenuation to stereo spatialization sounds
	uint8 bApplyNormalizationToStereoSounds:1;

	FSoundParseParameters()
		: SoundClass(nullptr)
		, Velocity(ForceInit)
		, Volume(1.f)
		, DistanceAttenuation(1.f)
		, VolumeMultiplier(1.f)
		, EnvelopeFollowerAttackTime(10)
		, EnvelopeFollowerReleaseTime(100)
		, InteriorVolumeMultiplier(1.f)
		, Pitch(1.f)
		, StartTime(-1.f)
		, OmniRadius(0.0f)
		, AttenuationDistance(0.0f)
		, ListenerToSoundDistance(0.0f)
		, ListenerToSoundDistanceForPanning(0.0f)
		, AbsoluteAzimuth(0.0f)
		, SoundSubmix(nullptr)
		, ReverbSendMethod(EReverbSendMethod::Linear)
		, ReverbSendLevelRange(0.0f, 0.0f)
		, ReverbSendLevelDistanceRange(0.0f, 0.0f)
		, ManualReverbSendLevel(0.0f)
		, StereoSpread(0.0f)
		, SpatializationMethod(ESoundSpatializationAlgorithm::SPATIALIZATION_Default)
		, SpatializationPluginSettings(nullptr)
		, OcclusionPluginSettings(nullptr)
		, ModulationPluginSettings(nullptr)
		, ReverbPluginSettings(nullptr)
		, SourceEffectChain(nullptr)
		, LowPassFilterFrequency(MAX_FILTER_FREQUENCY)
		, AttenuationLowpassFilterFrequency(MAX_FILTER_FREQUENCY)
		, AttenuationHighpassFilterFrequency(MIN_FILTER_FREQUENCY)
		, OcclusionFilterFrequency(MAX_FILTER_FREQUENCY)
		, AmbientZoneFilterFrequency(MAX_FILTER_FREQUENCY)
		, bOutputToBusOnly(false)
		, bUseSpatialization(false)
		, bLooping(false)
		, bEnableLowPassFilter(false)
		, bIsOccluded(false)
		, bIsPaused(false)
		, bEnableRetrigger(false)
		, bApplyNormalizationToStereoSounds(false)
	{
	}
};

struct ENGINE_API FActiveSound : public ISoundModulatable
{
public:

	FActiveSound();
	~FActiveSound();

	static FActiveSound* CreateVirtualCopy(const FActiveSound& ActiveSoundToCopy, FAudioDevice& AudioDevice);

private:
	TWeakObjectPtr<UWorld> World;
	uint32 WorldID;

	USoundBase* Sound;
	USoundEffectSourcePresetChain* SourceEffectChain;

	uint64 AudioComponentID;
	FName AudioComponentUserID;
	uint32 OwnerID;

	FName AudioComponentName;
	FName OwnerName;


public:
	// ISoundModulatable Implementation
	USoundModulationPluginSourceSettingsBase* FindModulationSettings() const override;
	uint32 GetObjectId() const override { return Sound ? Sound->GetUniqueID() : INDEX_NONE; }
	int32 GetPlayCount() const override;
	bool IsPreviewSound() const override { return bIsPreviewSound; }
	void Stop() override;


	uint64 GetAudioComponentID() const { return AudioComponentID; }
	FName GetAudioComponentUserID() const { return AudioComponentUserID; }
	void ClearAudioComponent();
	void SetAudioComponent(const FActiveSound& ActiveSound);
	void SetAudioComponent(const UAudioComponent& Component);
	void SetOwner(AActor* Owner);
	FString GetAudioComponentName() const;
	FString GetOwnerName() const;

	uint32 GetWorldID() const { return WorldID; }
	TWeakObjectPtr<UWorld> GetWeakWorld() const { return World; }
	UWorld* GetWorld() const
	{
		return World.Get();
	}
	void SetWorld(UWorld* World);

	void SetPitch(float Value);
	void SetVolume(float Value);

	float GetPitch() const { return PitchMultiplier; }

	/** Gets volume product all gain stages pertaining to active sound */
	float GetVolume() const;

	USoundBase* GetSound() const { return Sound; }
	void SetSound(USoundBase* InSound);

	USoundEffectSourcePresetChain* GetSourceEffectChain() const { return SourceEffectChain ? SourceEffectChain : Sound->SourceEffectChain; }
	void SetSourceEffectChain(USoundEffectSourcePresetChain* InSourceEffectChain);

	void SetSoundClass(USoundClass* SoundClass);

	void SetAudioDevice(FAudioDevice* InAudioDevice)
	{
		AudioDevice = InAudioDevice;
	}

	int32 GetClosestListenerIndex() const { return ClosestListenerIndex; }

	/** Returns whether or not the active sound can be deleted. */
	bool CanDelete() const { return !bAsyncOcclusionPending; }

	/** Whether or not the active sound is a looping sound. */
	bool IsLooping() const { return Sound && Sound->IsLooping(); }

	/** Whether or not the active sound a one-shot sound. */
	bool IsOneShot() const { return !IsLooping(); }

	/** Whether or not the active sound is currently playing audible sound. */
	bool IsPlayingAudio() const { return bIsPlayingAudio; }

	/** Whether or not sound reference is valid and set to play when silent. */
	bool IsPlayWhenSilent() const;

	FAudioDevice* AudioDevice;

	/** The concurrent groups that this sound is actively playing in. */
	TMap<FConcurrencyGroupID, FConcurrencySoundData> ConcurrencyGroupData;

	/** Optional USoundConcurrency to override for the sound. */
	TSet<USoundConcurrency*> ConcurrencySet;

private:
	/** Optional SoundClass to override for the sound. */
	USoundClass* SoundClassOverride;

	/** Optional override the submix sends for the sound. */
	TArray<FSoundSubmixSendInfo> SoundSubmixSendsOverride;

	/** Optional override for the source bus sends for the sound. */
	TArray<FSoundSourceBusSendInfo> SoundSourceBusSendsOverride[(int32)EBusSendType::Count];

	TMap<UPTRINT, FWaveInstance*> WaveInstances;

public:
	enum class EFadeOut : uint8
	{
		// Sound is not currently fading out
		None,

		// Client code (eg. AudioComponent) is requesting a fade out
		User,

		// The concurrency system is requesting a fade due to voice stealing
		Concurrency
	};

	/** Whether or not the sound has checked if it was occluded already. Used to initialize a sound as occluded and bypassing occlusion interpolation. */
	uint8 bHasCheckedOcclusion:1;

	/** Is this sound allowed to be spatialized? */
	uint8 bAllowSpatialization:1;

	/** Does this sound have attenuation settings specified. */
	uint8 bHasAttenuationSettings:1;

	/** Whether the wave instances should remain active if they're dropped by the prioritization code. Useful for e.g. vehicle sounds that shouldn't cut out. */
	uint8 bShouldRemainActiveIfDropped:1;

	/** Whether the current component has finished playing */
	uint8 bFinished:1;

	/** Whether or not the active sound is paused. Independently set vs global pause or unpause. */
	uint8 bIsPaused:1;

	/** Whether or not to stop this active sound due to max concurrency */
	uint8 bShouldStopDueToMaxConcurrency:1;

	/** Whether or not sound has been virtualized and then realized */
	uint8 bHasVirtualized:1;

	/** If true, the decision on whether to apply the radio filter has been made. */
	uint8 bRadioFilterSelected:1;

	/** If true, this sound will not be stopped when flushing the audio device. */
	uint8 bApplyRadioFilter:1;

	/** If true, the AudioComponent will be notified when a Wave is started to handle subtitles */
	uint8 bHandleSubtitles:1;

	/** If true, subtitles are being provided for the sound externally, so it still needs to make sure the sound plays to trigger the subtitles. */
	uint8 bHasExternalSubtitles:1;

	/** Whether the Location of the component is well defined */
	uint8 bLocationDefined:1;

	/** If true, this sound will not be stopped when flushing the audio device. */
	uint8 bIgnoreForFlushing:1;

	/** Whether to artificially prioritize the component to play */
	uint8 bAlwaysPlay:1;

	/** Whether or not this sound plays when the game is paused in the UI */
	uint8 bIsUISound:1;

	/** Whether or not this audio component is a music clip */
	uint8 bIsMusic:1;

	/** Whether or not the audio component should be excluded from reverb EQ processing */
	uint8 bReverb:1;

	/** Whether or not this sound class forces sounds to the center channel */
	uint8 bCenterChannelOnly:1;

	/** Whether or not this active sound is a preview sound */
	uint8 bIsPreviewSound:1;

	/** Whether we have queried for the interior settings at least once */
	uint8 bGotInteriorSettings:1;

	/** Whether some part of this sound will want interior sounds to be applied */
	uint8 bApplyInteriorVolumes:1;

#if !(NO_LOGGING || UE_BUILD_SHIPPING || UE_BUILD_TEST)
	/** For debugging purposes, output to the log once that a looping sound has been orphaned */
	uint8 bWarnedAboutOrphanedLooping:1;
#endif

	/** Whether or not we have a low-pass filter enabled on this active sound. */
	uint8 bEnableLowPassFilter : 1;

	/** Whether or not this active sound will update play percentage. Based on set delegates on audio component. */
	uint8 bUpdatePlayPercentage:1;

	/** Whether or not this active sound will update the envelope value of every wave instance that plays a sound source. Based on set delegates on audio component. */
	uint8 bUpdateSingleEnvelopeValue:1;

	/** Whether or not this active sound will update the average envelope value of every wave instance that plays a sound source. Based on set delegates on audio component. */
	uint8 bUpdateMultiEnvelopeValue:1;

	/** Whether or not the active sound should update it's owning audio component's playback time. */
	uint8 bUpdatePlaybackTime:1;

	/** Whether or not this active sound is playing audio, as in making audible sounds. */
	uint8 bIsPlayingAudio:1;

	/** Whether or not the active sound is stopping. */
	uint8 bIsStopping:1;

	uint8 UserIndex;

	/** Type of fade out currently being applied */
	EFadeOut FadeOut;

	/** whether we were occluded the last time we checked */
	FThreadSafeBool bIsOccluded;

	/** Whether or not there is an async occlusion trace pending */
	FThreadSafeBool bAsyncOcclusionPending;

	/** Duration between now and when the sound has been started. */
	float PlaybackTime;

	/** If virtualized, duration between last time virtualized and now. */
	float PlaybackTimeNonVirtualized;

	float MinCurrentPitch;
	float RequestedStartTime;

	float VolumeMultiplier;
	float PitchMultiplier;

	/** The low-pass filter frequency to apply if bEnableLowPassFilter is true. */
	float LowPassFilterFrequency;

	/** Fader that tracks component volume */
	Audio::FVolumeFader ComponentVolumeFader;

	/** The interpolated parameter for the low-pass frequency due to occlusion. */
	FDynamicParameter CurrentOcclusionFilterFrequency;

	/** The interpolated parameter for the volume attenuation due to occlusion. */
	FDynamicParameter CurrentOcclusionVolumeAttenuation;

	float SubtitlePriority;

	/** The product of the component priority and the USoundBase priority */
	float Priority;

	/** The volume used to determine concurrency resolution for "quietest" active sound.
	// If negative, tracking is disabled for lifetime of ActiveSound */
	float VolumeConcurrency;

	/** The time in seconds with which to check for occlusion from its closest listener */
	float OcclusionCheckInterval;

	/** Last time we checked for occlusion */
	float LastOcclusionCheckTime;

	/** The max distance this sound will be audible. */
	float MaxDistance;

	FTransform Transform;

	/**
	 * Cached data pertaining to focus system updated each frame
	 */
	FAttenuationFocusData FocusData;

	/** Location last time playback was updated */
	FVector LastLocation;

	FSoundAttenuationSettings AttenuationSettings;

	/** Cache what volume settings we had last time so we don't have to search again if we didn't move */
	FInteriorSettings InteriorSettings;

	uint32 AudioVolumeID;

	// To remember where the volumes are interpolating to and from
	double LastUpdateTime;
	float SourceInteriorVolume;
	float SourceInteriorLPF;
	float CurrentInteriorVolume;
	float CurrentInteriorLPF;

	// Envelope follower attack and release time parameters
	int32 EnvelopeFollowerAttackTime;
	int32 EnvelopeFollowerReleaseTime;

	TMap<UPTRINT,uint32> SoundNodeOffsetMap;
	TArray<uint8> SoundNodeData;

	TArray<FAudioComponentParam> InstanceParameters;

#if ENABLE_AUDIO_DEBUG
	FColor DebugColor;
#endif // ENABLE_AUDIO_DEBUG

	// Updates the wave instances to be played.
	void UpdateWaveInstances(TArray<FWaveInstance*> &OutWaveInstances, const float DeltaTime);

	/**
	 * Find an existing waveinstance attached to this audio component (if any)
	 */
	FWaveInstance* FindWaveInstance(const UPTRINT WaveInstanceHash);

	void RemoveWaveInstance(const UPTRINT WaveInstanceHash);

	const TMap<UPTRINT, FWaveInstance*>& GetWaveInstances() const
	{
		return WaveInstances;
	}

	/**
	 * Add newly created wave instance to active sound
	 */
	FWaveInstance& AddWaveInstance(const UPTRINT WaveInstanceHash);

	/**
	 * Check whether to apply the radio filter
	 */
	void ApplyRadioFilter(const FSoundParseParameters& ParseParams);

	/** Gets total concurrency gain stage based on all concurrency memberships of sound */
	float GetTotalConcurrencyVolumeScale() const;

	/** Sets a float instance parameter for the ActiveSound */
	void SetFloatParameter(const FName InName, const float InFloat);

	/** Sets a wave instance parameter for the ActiveSound */
	void SetWaveParameter(const FName InName, class USoundWave* InWave);

	/** Sets a boolean instance parameter for the ActiveSound */
	void SetBoolParameter(const FName InName, const bool InBool);

	/** Sets an integer instance parameter for the ActiveSound */
	void SetIntParameter(const FName InName, const int32 InInt);

	/** Sets the audio component parameter on the active sound. Note: this can be set without audio components if they are set when active sound is created. */
	void SetSoundParameter(const FAudioComponentParam& Param);

	/**
	 * Try and find an Instance Parameter with the given name and if we find it return the float value.
	 * @return true if float for parameter was found, otherwise false
	 */
	bool GetFloatParameter(const FName InName, float& OutFloat) const;

	/**
	 *Try and find an Instance Parameter with the given name and if we find it return the USoundWave value.
	 * @return true if USoundWave for parameter was found, otherwise false
	 */
	bool GetWaveParameter(const FName InName, USoundWave*& OutWave) const;

	/**
	 *Try and find an Instance Parameter with the given name and if we find it return the boolean value.
	 * @return true if boolean for parameter was found, otherwise false
	 */
	bool GetBoolParameter(const FName InName, bool& OutBool) const;

	/**
	 *Try and find an Instance Parameter with the given name and if we find it return the integer value.
	 * @return true if boolean for parameter was found, otherwise false
	 */
	bool GetIntParameter(const FName InName, int32& OutInt) const;

	void CollectAttenuationShapesForVisualization(TMultiMap<EAttenuationShape::Type, FBaseAttenuationSettings::AttenuationShapeDetails>& ShapeDetailsMap) const;

	/**
	 * Friend archive function used for serialization.
	 */
	friend FArchive& operator<<( FArchive& Ar, FActiveSound* ActiveSound );

	void AddReferencedObjects( FReferenceCollector& Collector );

	/**
	 * Get the sound class to apply on this sound instance
	 */
	USoundClass* GetSoundClass() const;

	/**
	* Get the sound submix to use for this sound instance
	*/
	USoundSubmix* GetSoundSubmix() const;

	/** Gets the sound submix sends to use for this sound instance. */
	void GetSoundSubmixSends(TArray<FSoundSubmixSendInfo>& OutSends) const;

	/** Gets the sound source bus sends to use for this sound instance. */
	void GetSoundSourceBusSends(EBusSendType BusSendType, TArray<FSoundSourceBusSendInfo>& OutSends) const;

	/* Determines which of the provided listeners is the closest to the sound */
	int32 FindClosestListener( const TArray<struct FListener>& InListeners ) const;

	/* Determines which listener is the closest to the sound */
	int32 FindClosestListener() const;

	/** Returns the unique ID of the active sound's owner if it exists. Returns 0 if the sound doesn't have an owner. */
	FSoundOwnerObjectID GetOwnerID() const { return OwnerID; }

	/** Gets the sound concurrency handles applicable to this sound instance*/
	void GetConcurrencyHandles(TArray<FConcurrencyHandle>& OutConcurrencyHandles) const;

	bool GetConcurrencyFadeDuration(float& OutFadeDuration) const;

	/** Delegate callback function when an async occlusion trace completes */
	static void OcclusionTraceDone(const FTraceHandle& TraceHandle, FTraceDatum& TraceDatum);

	/** Applies the active sound's attenuation settings to the input parse params using the given listener */
	UE_DEPRECATED(4.25, "Use ParseAttenuation that passes a ListenerIndex instead")
	void ParseAttenuation(FSoundParseParameters& OutParseParams, const FListener& InListener, const FSoundAttenuationSettings& InAttenuationSettings);

	/** Applies the active sound's attenuation settings to the input parse params using the given listener */
	void ParseAttenuation(FSoundParseParameters& OutParseParams, int32 ListenerIndex, const FSoundAttenuationSettings& InAttenuationSettings);

	/** Returns the highest effective priority of the child wave instances */
	float GetHighestPriority() const { return Priority * FocusData.PriorityHighest * FocusData.PriorityScale; }

	/** Sets the amount of audio from this active sound to send to the submix. */
	void SetSubmixSend(const FSoundSubmixSendInfo& SubmixSendInfo);

	/** Sets the amount of audio from this active sound to send to the source bus. */
	void SetSourceBusSend(EBusSendType BusSendTyoe, const FSoundSourceBusSendInfo& SourceBusSendInfo);

	/** Updates the active sound's attenuation settings to the input parse params using the given listener */
	UE_DEPRECATED(4.25, "Use UpdateAttenuation that passes a ListenerIndex instead")
	void UpdateAttenuation(float DeltaTime, FSoundParseParameters& ParseParams, const FListener& Listener, const FSoundAttenuationSettings* SettingsAttenuationNode = nullptr);

	/** Updates the active sound's attenuation settings to the input parse params using the given listener */
	void UpdateAttenuation(float DeltaTime, FSoundParseParameters& ParseParams, int32 ListenerIndex, const FSoundAttenuationSettings* SettingsAttenuationNode = nullptr);

	/** Updates the provided focus data using the local */
	void UpdateFocusData(float DeltaTime, const FAttenuationListenerData& ListenerData, FAttenuationFocusData* OutFocusData = nullptr);

private:

	struct FAsyncTraceDetails
	{
		Audio::FDeviceId AudioDeviceID;
		FActiveSound* ActiveSound;
	};

	static TMap<FTraceHandle, FAsyncTraceDetails> TraceToActiveSoundMap;

	static FTraceDelegate ActiveSoundTraceDelegate;

	/** Cached index to the closest listener. So we don't have to do the work to find it twice. */
	int32 ClosestListenerIndex;

	/** This is a friend so the audio device can call Stop() on the active sound. */
	friend class FAudioDevice;

	/**
	  * Marks the active sound as pending delete and begins termination of internal resources.
	  * Only to be called from the owning audio device.
	  */
	void MarkPendingDestroy (bool bDestroyNow);

	/** Whether or not the active sound is stopping. */
	bool IsStopping() const { return bIsStopping; }

	/** Called when an active sound has been stopped but needs to update it's stopping sounds. Returns true when stopping sources have finished stopping. */
	bool UpdateStoppingSources(uint64 CurrentTick, bool bEnsureStopped);

	/** Updates ramping concurrency volume scalars */
	void UpdateConcurrencyVolumeScalars(const float DeltaTime);

	/** if OcclusionCheckInterval > 0.0, checks if the sound has become (un)occluded during playback
	 * and calls eventOcclusionChanged() if so
	 * primarily used for gameplay-relevant ambient sounds
	 * CurrentLocation is the location of this component that will be used for playback
	 * @param ListenerLocation location of the closest listener to the sound
	 */
	void CheckOcclusion(const FVector ListenerLocation, const FVector SoundLocation, const FSoundAttenuationSettings* AttenuationSettingsPtr);

	/** Apply the interior settings to the ambient sound as appropriate */
	void HandleInteriorVolumes(struct FSoundParseParameters& ParseParams);

	/** Helper function which retrieves attenuation frequency value for HPF and LPF distance-based filtering. */
	float GetAttenuationFrequency(const FSoundAttenuationSettings* InSettings, const FAttenuationListenerData& ListenerData, const FVector2D& FrequencyRange, const FRuntimeFloatCurve& CustomCurve);
};