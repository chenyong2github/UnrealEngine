// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Components/SceneComponent.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "IAudioExtensionPlugin.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"
#include "UObject/ObjectMacros.h"
#include "Math/RandomStream.h"
#include "Sound/QuartzSubscription.h"
#include "Sound/QuartzQuantizationUtilities.h"
#include "Quartz/AudioMixerClockHandle.h"
#include "Quartz/AudioMixerQuantizedCommands.h"

#include "AudioComponent.generated.h"

class FAudioDevice;
class USoundBase;
class USoundClass;
class USoundConcurrency;

// Enum describing the audio component play state
UENUM(BlueprintType)
enum class EAudioComponentPlayState : uint8
{
	// If the sound is playing (i.e. not fading in, not fading out, not paused)
	Playing,

	// If the sound is not playing
	Stopped, 

	// If the sound is playing but paused
	Paused,

	// If the sound is playing and fading in
	FadingIn,

	// If the sound is playing and fading out
	FadingOut,

	Count UMETA(Hidden)
};


/** called when we finish playing audio, either because it played to completion or because a Stop() call turned it off early */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAudioFinished);

/** shadow delegate declaration for above */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnAudioFinishedNative, class UAudioComponent*);

/** Called when subtitles are sent to the SubtitleManager.  Set this delegate if you want to hijack the subtitles for other purposes */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnQueueSubtitles, const TArray<struct FSubtitleCue>&, Subtitles, float, CueDuration);

/** Called when sound's PlayState changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAudioPlayStateChanged, EAudioComponentPlayState, PlayState);

/** shadow delegate declaration for above */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAudioPlayStateChangedNative, const class UAudioComponent*, EAudioComponentPlayState);

/** Called when sound becomes virtualized or realized (resumes playback from virtualization). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAudioVirtualizationChanged, bool, bIsVirtualized);

/** shadow delegate declaration for above */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAudioVirtualizationChangedNative, const class UAudioComponent*, bool);

/** Called as a sound plays on the audio component to allow BP to perform actions based on playback percentage.
* Computed as samples played divided by total samples, taking into account pitch.
* Not currently implemented on all platforms.
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAudioPlaybackPercent, const class USoundWave*, PlayingSoundWave, const float, PlaybackPercent);

/** shadow delegate declaration for above */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAudioPlaybackPercentNative, const class UAudioComponent*, const class USoundWave*, const float);

/**
* Called while a sound plays and returns the sound's envelope value (using an envelope follower in the audio renderer).
* This only works in the audio mixer.
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAudioSingleEnvelopeValue, const class USoundWave*, PlayingSoundWave, const float, EnvelopeValue);

/** shadow delegate declaration for above */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAudioSingleEnvelopeValueNative, const class UAudioComponent*, const class USoundWave*, const float);

/**
* Called while a sound plays and returns the sound's average and max envelope value (using an envelope follower in the audio renderer per wave instance).
* This only works in the audio mixer.
*/
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnAudioMultiEnvelopeValue, const float, AverageEnvelopeValue, const float, MaxEnvelope, const int32, NumWaveInstances);

/** shadow delegate declaration for above */
DECLARE_MULTICAST_DELEGATE_FourParams(FOnAudioMultiEnvelopeValueNative, const class UAudioComponent*, const float, const float, const int32);



/** Type of fade to use when adjusting the audio component's volume. */
UENUM(BlueprintType)
enum class EAudioFaderCurve : uint8
{
	// Linear Fade
	Linear,

	// Logarithmic Fade
	Logarithmic,

	// S-Curve, Sinusoidal Fade
	SCurve UMETA(DisplayName = "Sin (S-Curve)"),

	// Equal Power, Sinusoidal Fade
	Sin UMETA(DisplayName = "Sin (Equal Power)"),

	Count UMETA(Hidden)
};


/**
 *	Struct used for storing one per-instance named parameter for this AudioComponent.
 *	Certain nodes in the SoundCue may reference parameters by name so they can be adjusted per-instance.
 */
USTRUCT(BlueprintType)
struct FAudioComponentParam
{
	GENERATED_USTRUCT_BODY()

	// Name of the parameter
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=AudioComponentParam)
	FName ParamName;

	// Value of the parameter when used as a float
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=AudioComponentParam)
	float FloatParam;

	// Value of the parameter when used as a boolean
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=AudioComponentParam)
	bool BoolParam;

	// Value of the parameter when used as an integer
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=AudioComponentParam)
	int32 IntParam;

	// Value of the parameter when used as a sound wave
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=AudioComponentParam)
	class USoundWave* SoundWaveParam;

	FAudioComponentParam(const FName& Name)
		: ParamName(Name)
		, FloatParam(0.f)
		, BoolParam(false)
		, IntParam(0)
		, SoundWaveParam(nullptr)
	{}

	FAudioComponentParam()
		: FloatParam(0.f)
		, BoolParam(false)
		, IntParam(0)
		, SoundWaveParam(nullptr)
	{
	}

};

/**
 * AudioComponent is used to play a Sound
 *
 * @see https://docs.unrealengine.com/latest/INT/Audio/Overview/index.html
 * @see USoundBase
 */
UCLASS(ClassGroup=(Audio, Common), hidecategories=(Object, ActorComponent, Physics, Rendering, Mobility, LOD), ShowCategories=Trigger, meta=(BlueprintSpawnableComponent))
class ENGINE_API UAudioComponent : public USceneComponent
{
	GENERATED_UCLASS_BODY()

	/** The sound to be played */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Sound)
	class USoundBase* Sound;

	/** Array of per-instance parameters for this AudioComponent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Sound, AdvancedDisplay)
	TArray<struct FAudioComponentParam> InstanceParameters;

	/** Optional sound group this AudioComponent belongs to */
	UPROPERTY(EditAnywhere, Category=Sound, AdvancedDisplay)
	USoundClass* SoundClassOverride;

	/** Auto destroy this component on completion */
	UPROPERTY()
	uint8 bAutoDestroy:1;

	/** Stop sound when owner is destroyed */
	UPROPERTY()
	uint8 bStopWhenOwnerDestroyed:1;

	/** Whether the wave instances should remain active if they're dropped by the prioritization code. Useful for e.g. vehicle sounds that shouldn't cut out. */
	UPROPERTY()
	uint8 bShouldRemainActiveIfDropped:1;

	/** Overrides spatialization enablement in either the attenuation asset or on this audio component's attenuation settings override. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Attenuation)
	uint8 bAllowSpatialization:1;

	/** Allows defining attenuation settings directly on this audio component without using an attenuation settings asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Attenuation)
	uint8 bOverrideAttenuation:1;

	/** Whether or not to override the sound's subtitle priority. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sound)
	uint8 bOverrideSubtitlePriority:1;

	/** Whether or not this sound plays when the game is paused in the UI */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sound)
	uint8 bIsUISound : 1;

	/** Whether or not to apply a low-pass filter to the sound that plays in this audio component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Filter)
	uint8 bEnableLowPassFilter : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sound)
	uint8 bOverridePriority:1;

	/** If true, subtitles in the sound data will be ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sound)
	uint8 bSuppressSubtitles:1;

	/** Whether this audio component is previewing a sound */
	uint8 bPreviewComponent:1;

	/** If true, this sound will not be stopped when flushing the audio device. */
	uint8 bIgnoreForFlushing:1;

	/** Whether to artificially prioritize the component to play */
	uint8 bAlwaysPlay:1;

	/** Whether or not this audio component is a music clip */
	uint8 bIsMusic:1;

	/** Whether or not the audio component should be excluded from reverb EQ processing */
	uint8 bReverb:1;

	/** Whether or not this sound class forces sounds to the center channel */
	uint8 bCenterChannelOnly:1;

	/** Whether or not this sound is a preview sound */
	uint8 bIsPreviewSound:1;

	/** Whether or not this audio component has been paused */
	uint8 bIsPaused:1;

	/** Whether or not this audio component's sound is virtualized */
	uint8 bIsVirtualized:1;

	/** Whether or not fade out was triggered. */
	uint8 bIsFadingOut:1;

	/**
	* True if we should automatically attach to AutoAttachParent when Played, and detach from our parent when playback is completed.
	* This overrides any current attachment that may be present at the time of activation (deferring initial attachment until activation, if AutoAttachParent is null).
	* If enabled, this AudioComponent's WorldLocation will no longer be reliable when not currently playing audio, and any attach children will also be
	* detached/attached along with it.
	* When enabled, detachment occurs regardless of whether AutoAttachParent is assigned, and the relative transform from the time of activation is restored.
	* This also disables attachment on dedicated servers, where we don't actually activate even if bAutoActivate is true.
	* @see AutoAttachParent, AutoAttachSocketName, AutoAttachLocationType
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Attachment)
	uint8 bAutoManageAttachment:1;

private:
	/** Did we auto attach during activation? Used to determine if we should restore the relative transform during detachment. */
	uint8 bDidAutoAttach : 1;

public:
	/** The specific audio device to play this component on */
	uint32 AudioDeviceID;

	/** Configurable, serialized ID for audio plugins */
	UPROPERTY()
	FName AudioComponentUserID;

	/** The lower bound to use when randomly determining a pitch multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Randomization|Pitch", meta = (DisplayName = "Pitch (Min)"))
	float PitchModulationMin;

	/** The upper bound to use when randomly determining a pitch multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Randomization|Pitch", meta = (DisplayName = "Pitch (Max)"))
	float PitchModulationMax;

	/** The lower bound to use when randomly determining a volume multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Randomization|Volume", meta = (DisplayName = "Volume (Min)"))
	float VolumeModulationMin;

	/** The upper bound to use when randomly determining a volume multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= "Randomization|Volume", meta = (DisplayName = "Volume (Max)"))
	float VolumeModulationMax;

	/** A volume multiplier to apply to sounds generated by this component */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Sound)
	float VolumeMultiplier;

	/** The attack time in milliseconds for the envelope follower. Delegate callbacks can be registered to get the 
	 *  envelope value of sounds played with this audio component. Only used in audio mixer. 
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sound, meta = (ClampMin = "0", UIMin = "0"))
	int32 EnvelopeFollowerAttackTime;

	/** The release time in milliseconds for the envelope follower. Delegate callbacks can be registered to get the
	 *  envelope value of sounds played with this audio component. Only used in audio mixer. 
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sound, meta = (ClampMin = "0", UIMin = "0"))
	int32 EnvelopeFollowerReleaseTime;

	/** A priority value that is used for sounds that play on this component that scales against final output volume. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sound, meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bOverridePriority"))
	float Priority;

	/** Used by the subtitle manager to prioritize subtitles wave instances spawned by this component. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sound, meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bOverrideSubtitlePriority"))
	float SubtitlePriority;

	/** The chain of Source Effects to apply to the sounds playing on the Audio Component */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Sound)
	USoundEffectSourcePresetChain* SourceEffectChain;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	float VolumeWeightedPriorityScale_DEPRECATED;

	UPROPERTY()
	float HighFrequencyGainMultiplier_DEPRECATED;
#endif

	/** A pitch multiplier to apply to sounds generated by this component */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Sound)
	float PitchMultiplier;

	/** The frequency of the Lowpass Filter (in Hz) to apply to this voice. A frequency of 0.0 is the device sample rate and will bypass the filter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Filter, meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableLowPassFilter"))
	float LowPassFilterFrequency;

	/** A count of how many times we've started playing */
	int32 ActiveCount;

	/** If bOverrideSettings is false, the asset to use to determine attenuation properties for sounds generated by this component */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Attenuation, meta=(EditCondition="!bOverrideAttenuation"))
	class USoundAttenuation* AttenuationSettings;

	/** If bOverrideSettings is true, the attenuation properties to use for sounds generated by this component */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category= Attenuation, meta=(EditCondition="bOverrideAttenuation"))
	struct FSoundAttenuationSettings AttenuationOverrides;

	/** What sound concurrency to use for sounds generated by this audio component */
	UPROPERTY()
	USoundConcurrency* ConcurrencySettings_DEPRECATED;

	/** What sound concurrency rules to use for sounds generated by this audio component */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Concurrency)
	TSet<USoundConcurrency*> ConcurrencySet;

	/** While playing, this component will check for occlusion from its closest listener every this many seconds */
	float OcclusionCheckInterval;

	/** What time the audio component was told to play. Used to compute audio component state. */
	float TimeAudioComponentPlayed;

	/** How much time the audio component was told to fade in. */
	float FadeInTimeDuration;

	/**
	 * Options for how we handle our location when we attach to the AutoAttachParent, if bAutoManageAttachment is true.
	 * @see bAutoManageAttachment, EAttachmentRule
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attachment, meta = (EditCondition = "bAutoManageAttachment"))
	EAttachmentRule AutoAttachLocationRule;

	/**
	 * Options for how we handle our rotation when we attach to the AutoAttachParent, if bAutoManageAttachment is true.
	 * @see bAutoManageAttachment, EAttachmentRule
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attachment, meta = (EditCondition = "bAutoManageAttachment"))
	EAttachmentRule AutoAttachRotationRule;

	/**
	 * Options for how we handle our scale when we attach to the AutoAttachParent, if bAutoManageAttachment is true.
	 * @see bAutoManageAttachment, EAttachmentRule
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Attachment, meta = (EditCondition = "bAutoManageAttachment"))
	EAttachmentRule AutoAttachScaleRule;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Modulation)
	FSoundModulationDefaultRoutingSettings ModulationRouting;

	/** This function returns the Targeted Audio Component’s current Play State.
	  * Playing, if the sound is currently playing.
	  * Stopped, if the sound is stopped.
	  * Paused, if the sound is currently playing, but paused.
	  * Fading In, if the sound is in the process of Fading In.
	  * Fading Out, if the sound is in the process of Fading Out.
	  */
	UPROPERTY(BlueprintAssignable)
	FOnAudioPlayStateChanged OnAudioPlayStateChanged;

	/** Shadow delegate for non UObject subscribers */
	FOnAudioPlayStateChangedNative OnAudioPlayStateChangedNative;

	/** Called when virtualization state changes */
	UPROPERTY(BlueprintAssignable)
	FOnAudioVirtualizationChanged OnAudioVirtualizationChanged;

	/** Shadow delegate for non UObject subscribers */
	FOnAudioVirtualizationChangedNative OnAudioVirtualizationChangedNative;

	/** Called when we finish playing audio, either because it played to completion or because a Stop() call turned it off early */
	UPROPERTY(BlueprintAssignable)
	FOnAudioFinished OnAudioFinished;

	/** Shadow delegate for non UObject subscribers */
	FOnAudioFinishedNative OnAudioFinishedNative;

	/** Called as a sound plays on the audio component to allow BP to perform actions based on playback percentage.
	 *  Computed as samples played divided by total samples, taking into account pitch.
	 *  Not currently implemented on all platforms.
	*/
	UPROPERTY(BlueprintAssignable)
	FOnAudioPlaybackPercent OnAudioPlaybackPercent;

	/** Shadow delegate for non UObject subscribers */
	FOnAudioPlaybackPercentNative OnAudioPlaybackPercentNative;

	UPROPERTY(BlueprintAssignable)
	FOnAudioSingleEnvelopeValue OnAudioSingleEnvelopeValue;

	/** Shadow delegate for non UObject subscribers */
	FOnAudioSingleEnvelopeValueNative OnAudioSingleEnvelopeValueNative;

	UPROPERTY(BlueprintAssignable)
	FOnAudioMultiEnvelopeValue OnAudioMultiEnvelopeValue;

	/** Shadow delegate for non UObject subscribers */
	FOnAudioMultiEnvelopeValueNative OnAudioMultiEnvelopeValueNative;

	/** Called when subtitles are sent to the SubtitleManager.  Set this delegate if you want to hijack the subtitles for other purposes */
	UPROPERTY()
	FOnQueueSubtitles OnQueueSubtitles;

	// Set what sound is played by this component
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio")
	void SetSound( USoundBase* NewSound );

	/**
	 * This function allows designers to call Play on an Audio Component instance while applying a volume curve over time. 
	 * Parameters allow designers to indicate the duration of the fade, the curve shape, and the start time if seeking into the sound.
	 *
	 * @param FadeInDuration How long it should take to reach the FadeVolumeLevel
	 * @param FadeVolumeLevel The percentage of the AudioComponents's calculated volume to fade to
	 * @param FadeCurve The curve to use when interpolating between the old and new volume
	 */
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio")
	virtual void FadeIn(float FadeInDuration, float FadeVolumeLevel = 1.0f, float StartTime = 0.0f, const EAudioFaderCurve FadeCurve = EAudioFaderCurve::Linear);

	/**
	 * This function allows designers to call a delayed Stop on an Audio Component instance while applying a
	 * volume curve over time. Parameters allow designers to indicate the duration of the fade and the curve shape.
	 *
	 * @param FadeOutDuration how long it should take to reach the FadeVolumeLevel
	 * @param FadeVolumeLevel the percentage of the AudioComponents's calculated volume in which to fade to
	 * @param FadeCurve The curve to use when interpolating between the old and new volume
	 */
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio")
	virtual	void FadeOut(float FadeOutDuration, float FadeVolumeLevel, const EAudioFaderCurve FadeCurve = EAudioFaderCurve::Linear);

	/** Begins playing the targeted Audio Component’s sound at the designated Start Time, seeking into a sound. 
	 * @param StartTime The offset, in seconds, to begin reading the sound at
	 */
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio")
	virtual void Play(float StartTime = 0.0f);

	/** Start a sound playing on an audio component on a given quantization boundary with the handle to an existing clock */
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio", meta=(WorldContext = "WorldContextObject", AdvancedDisplay = "3", UnsafeDuringActorConstruction = "true", Keywords = "play", AutoCreateRefTerm = "InDelegate"))
	virtual void PlayQuantized(
		  const UObject* WorldContextObject
		, UPARAM(ref) UQuartzClockHandle*& InClockHandle
		, UPARAM(ref) FQuartzQuantizationBoundary& InQuantizationBoundary
		, const FOnQuartzCommandEventBP& InDelegate
		, float InStartTime = 0.f
		, float InFadeInDuration = 0.f
		, float InFadeVolumeLevel = 1.f
		, EAudioFaderCurve InFadeCurve = EAudioFaderCurve::Linear
	);

	/** Stop an audio component's sound, issue any delegates if needed */
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio")
	virtual void Stop();

	/** Cues request to stop sound after the provided delay (in seconds), stopping immediately if delay is zero or negative */
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio")
	void StopDelayed(float DelayTime);

	/** Pause an audio component playing its sound cue, issue any delegates if needed */
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	void SetPaused(bool bPause);

	/** Returns TRUE if the targeted Audio Component’s sound is playing. 
	 *  Doesn't indicate if the sound is paused or fading in/out. Use GetPlayState() to get the full play state.
	 */
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio")
	virtual bool IsPlaying() const;

	/** Returns if the sound is virtualized. */
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio")
	bool IsVirtualized() const;

	/** Returns the enumerated play states of the audio component. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	EAudioComponentPlayState GetPlayState() const;

	/** This function allows designers to trigger an adjustment to the sound instance’s playback Volume with options for smoothly applying a curve over time.
	 * @param AdjustVolumeDuration The length of time in which to interpolate between the initial volume and the new volume.
	 * @param AdjustVolumeLevel The new volume to set the Audio Component to.
	 * @param FadeCurve The curve used when interpolating between the old and new volume.
	 */
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio")
	void AdjustVolume(float AdjustVolumeDuration, float AdjustVolumeLevel, const EAudioFaderCurve FadeCurve = EAudioFaderCurve::Linear);

	/** Allows the designer to set the Float Parameter on the SoundCue whose name matches the name indicated.
	 * @param InName The name of the Float to set. It must match the name set in SoundCue's Crossfade By Param or Continuous Modulator Node.
	 * @param InFloat The value to set the Parameter to.
	 */
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio")
	void SetFloatParameter(FName InName, float InFloat);

	/** Allows the designer to set the Wave Parameter on the SoundCue whose name matches the name indicated.
	 * @param InName The name of the Wave to set. It must match the name set in SoundCue's WaveParam Node
	 * @param InWave The value to set the Parameter to
	 */
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio")
	void SetWaveParameter(FName InName, class USoundWave* InWave);

	/** Allows the designer to set the Boolean Parameter on the SoundCue whose name matches the name indicated.
	 * @param InName The name of the Boolean to set. It must match the name set in SoundCue's Branch Node 
	 * @param InBool The value to set the Parameter to
	 */
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio", meta=(DisplayName="Set Boolean Parameter"))
	void SetBoolParameter(FName InName, bool InBool);

	/** Allows the designer to set the Integer Parameter on the SoundCue whose name matches the name indicated.
	 * @param InName The name of the Integer to set. It must match the name set in SoundCue's Switch Node
	 * @param InInt The value to set the Parameter to
	 */
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio", meta=(DisplayName="Set Integer Parameter"))
	void SetIntParameter(FName InName, int32 InInt);

	/** Set a new volume multiplier */
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio")
	void SetVolumeMultiplier(float NewVolumeMultiplier);

	/** Set a new pitch multiplier */
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio")
	void SetPitchMultiplier(float NewPitchMultiplier);

	/** Set whether sounds generated by this audio component should be considered UI sounds */
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio")
	void SetUISound(bool bInUISound);

	/** This function is used to modify the Attenuation Settings on the targeted Audio Component instance. It is worth noting that Attenuation Settings are only passed to new Active Sounds on start, so modified Attenuation data should be set before sound playback. */
	UFUNCTION(BlueprintCallable, Category="Audio|Components|Audio")
	void AdjustAttenuation(const FSoundAttenuationSettings& InAttenuationSettings);

	/** Allows designers to target a specific Audio Component instance’s sound set the send level (volume of sound copied) to the indicated Submix.
	 * @param Submix The Submix to send the signal to.
	 * @param SendLevel The scalar used to alter the volume of the copied signal.*/
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	void SetSubmixSend(USoundSubmixBase* Submix, float SendLevel);

	/** Allows designers to target a specific Audio Component instance’s sound and set the send level (volume of sound copied)
	 *  to the indicated Source Bus. If the Source Bus is not already part of the sound’s sends, the reference will be added to
	 *  this instance’s Override sends. This particular send occurs before the Source Effect processing chain.
	 * @param SoundSourceBus The Bus to send the signal to.
	 * @param SourceBusSendLevel The scalar used to alter the volume of the copied signal.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	void SetSourceBusSendPreEffect(USoundSourceBus* SoundSourceBus, float SourceBusSendLevel);

	/** Allows designers to target a specific Audio Component instance’s sound and set the send level (volume of sound copied)
	 *  to the indicated Source Bus. If the Source Bus is not already part of the sound’s sends, the reference will be added to
	 *  this instance’s Override sends. This particular send occurs after the Source Effect processing chain.
	 * @param SoundSourceBus The Bus to send the signal to
	 * @param SourceBusSendLevel The scalar used to alter the volume of the copied signal
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	void SetSourceBusSendPostEffect(USoundSourceBus* SoundSourceBus, float SourceBusSendLevel);

	/** Sets how much audio the sound should send to the given Audio Bus (PRE Source Effects).
	 *  if the Bus Send doesn't already exist, it will be added to the overrides on the active sound. 
	 * @param AudioBus The Bus to send the signal to
	 * @param AudioBusSendLevel The scalar used to alter the volume of the copied signal
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	void SetAudioBusSendPreEffect(UAudioBus* AudioBus, float AudioBusSendLevel);

	/** Sets how much audio the sound should send to the given Audio Bus (POST Source Effects).
	 *  if the Audio Bus Send doesn't already exist, it will be added to the overrides on the active sound. 
	 * @param AudioBus The Bus to send the signal to
	 * @param AudioBusSendLevel The scalar used to alter the volume of the copied signal
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	void SetAudioBusSendPostEffect(UAudioBus* AudioBus, float AudioBusSendLevel);

	/** When set to TRUE, enables an additional Low Pass Filter Frequency to be calculated in with the
	 *  sound instance’s LPF total, allowing designers to set filter settings for the targeted Audio Component’s
	 *  sound instance.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	void SetLowPassFilterEnabled(bool InLowPassFilterEnabled);

	/** Sets a cutoff frequency, in Hz, for the targeted Audio Component’s sound’s Low Pass Filter calculation.
	 *  The lowest cutoff frequency from all of the sound instance’s possible LPF calculations wins.
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	void SetLowPassFilterFrequency(float InLowPassFilterFrequency);

	/** Sets whether or not to output the audio to bus only. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	void SetOutputToBusOnly(bool bInOutputToBusOnly);

	/** Queries if the sound wave playing in this audio component has cooked FFT data, returns FALSE if none found.  */
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	bool HasCookedFFTData() const;

	/** Queries whether or not the targeted Audio Component instance’s sound has Envelope Data, returns FALSE if none found. */
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	bool HasCookedAmplitudeEnvelopeData() const;

	/**
	* Retrieves the current-time cooked spectral data of the sounds playing on the audio component.
	* Spectral data is averaged and interpolated for all playing sounds on this audio component.
	* Returns true if there is data and the audio component is playing.
	*/
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	bool GetCookedFFTData(const TArray<float>& FrequenciesToGet, TArray<FSoundWaveSpectralData>& OutSoundWaveSpectralData);

	/**
	* Retrieves the current-time cooked spectral data of the sounds playing audio component.
	* Spectral data is not averaged or interpolated. Instead an array of data with all playing sound waves with cooked data is returned.
	* Returns true if there is data and the audio component is playing.
	*/
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	bool GetCookedFFTDataForAllPlayingSounds(TArray<FSoundWaveSpectralDataPerSound>& OutSoundWaveSpectralData);

	/**
	 * Retrieves Cooked Envelope Data at the current playback time. If there are multiple
	 * SoundWaves playing, data is interpolated and averaged across all playing sound waves.
	 * Returns FALSE if no data was found.
	*/
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	bool GetCookedEnvelopeData(float& OutEnvelopeData);

	/**
	* Retrieves the current-time envelope data of the sounds playing audio component.
	* Envelope data is not averaged or interpolated. Instead an array of data with all playing sound waves with cooked data is returned.
	* Returns true if there is data and the audio component is playing.
	*/
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio")
	bool GetCookedEnvelopeDataForAllPlayingSounds(TArray<FSoundWaveEnvelopeDataPerSound>& OutEnvelopeData);

	static void PlaybackCompleted(uint64 AudioComponentID, bool bFailedToStart);

private:
	/** Called by the ActiveSound to inform the component that playback is finished */
	void PlaybackCompleted(bool bFailedToStart);

	/** Whether or not the sound is audible. */
	bool IsInAudibleRange(float* OutMaxDistance) const;

	void SetBusSendffectInternal(USoundSourceBus* InSourceBus, UAudioBus* InAudioBus, float SendLevel, EBusSendType InBusSendType);

	void BroadcastPlayState();

public:

	/** Sets the sound instance parameter. */
	void SetSoundParameter(const FAudioComponentParam& Param);

	/** Set when the sound is finished with initial fading in */
	void SetFadeInComplete();

	/** Sets whether or not sound instance is virtualized */
	void SetIsVirtualized(bool bInIsVirtualized);

	//~ Begin UObject Interface.
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	virtual FString GetDetailedInfoInternal() const override;
	virtual void PostLoad() override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void BeginDestroy() override;
	//~ End UObject Interface.

	//~ Begin USceneComponent Interface
	virtual void Activate(bool bReset=false) override;
	virtual void Deactivate() override;
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport = ETeleportType::None) override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End USceneComponent Interface

	//~ Begin ActorComponent Interface.
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual const UObject* AdditionalStatObject() const override;
	virtual bool IsReadyForOwnerToAutoDestroy() const override;
	//~ End ActorComponent Interface.

	void AdjustVolumeInternal(float AdjustVolumeDuration, float AdjustVolumeLevel, bool bIsFadeOut, EAudioFaderCurve FadeCurve);

	/** Returns a pointer to the attenuation settings to be used (if any) for this audio component dependent on the SoundAttenuation asset or overrides set. */
	const FSoundAttenuationSettings* GetAttenuationSettingsToApply() const;

	/** Retrieves Attenuation Settings data on the targeted Audio Component. Returns FALSE if no settings were found. 
	 *  Because the Attenuation Settings data structure is copied, FALSE returns will return default values. 
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|Components|Audio", meta = (DisplayName = "Get Attenuation Settings To Apply", ScriptName="GetAttenuationSettingsToApply"))
	bool BP_GetAttenuationSettingsToApply(FSoundAttenuationSettings& OutAttenuationSettings);

	/** Collects the various attenuation shapes that may be applied to the sound played by the audio component for visualization
	 * in the editor or via the in game debug visualization. 
	 */
	void CollectAttenuationShapesForVisualization(TMultiMap<EAttenuationShape::Type, FBaseAttenuationSettings::AttenuationShapeDetails>& ShapeDetailsMap) const;

	/** Returns the active audio device to use for this component based on whether or not the component is playing in a world. */
	FAudioDevice* GetAudioDevice() const;

	uint64 GetAudioComponentID() const { return AudioComponentID; }

	FName GetAudioComponentUserID() const { return AudioComponentUserID; }

	static UAudioComponent* GetAudioComponentFromID(uint64 AudioComponentID);

	// Sets the audio thread playback time as used by the active sound playing this audio component
	// Will be set if the audio component is using baked FFT or envelope following data so as to be able to feed that data to BP based on playback time
	void SetPlaybackTimes(const TMap<uint32, float>& InSoundWavePlaybackTimes);

	void SetSourceEffectChain(USoundEffectSourcePresetChain* InSourceEffectChain);
public:

	/**
	 * Component we automatically attach to when activated, if bAutoManageAttachment is true.
	 * If null during registration, we assign the existing AttachParent and defer attachment until we activate.
	 * @see bAutoManageAttachment
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadWrite, Category=Attachment, meta=(EditCondition="bAutoManageAttachment"))
	TWeakObjectPtr<USceneComponent> AutoAttachParent;

	/**
	 * Socket we automatically attach to on the AutoAttachParent, if bAutoManageAttachment is true.
	 * @see bAutoManageAttachment
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Attachment, meta=(EditCondition="bAutoManageAttachment"))
	FName AutoAttachSocketName;

	struct PlayInternalRequestData
	{
		// start time
		float StartTime = 0.0f;

		// fade data
		float FadeInDuration = 0.0f;
		float FadeVolumeLevel = 1.0f;
		EAudioFaderCurve FadeCurve = EAudioFaderCurve::Linear;

		// Quantized event data
		Audio::FQuartzQuantizedRequestData QuantizedRequestData;
	};

private:

	uint64 AudioComponentID;

	float RetriggerTimeSinceLastUpdate;
	float RetriggerUpdateInterval;

	/** Saved relative transform before auto attachment. Used during detachment to restore the transform if we had automatically attached. */
	FVector SavedAutoAttachRelativeLocation;
	FRotator SavedAutoAttachRelativeRotation;
	FVector SavedAutoAttachRelativeScale3D;

	struct FSoundWavePlaybackTimeData
	{
		USoundWave* SoundWave;
		float PlaybackTime;

		// Cached indices to boost searching cooked data indices
		uint32 LastEnvelopeCookedIndex;
		uint32 LastFFTCookedIndex;

		FSoundWavePlaybackTimeData()
			: SoundWave(nullptr)
			, PlaybackTime(0.0f)
			, LastEnvelopeCookedIndex(INDEX_NONE)
			, LastFFTCookedIndex(INDEX_NONE)
		{}

		FSoundWavePlaybackTimeData(USoundWave* InSoundWave)
			: SoundWave(InSoundWave)
			, PlaybackTime(0.0f)
			, LastEnvelopeCookedIndex(INDEX_NONE)
			, LastFFTCookedIndex(INDEX_NONE)
		{}
	};
	// The current playback times of sound waves in this audio component
	TMap<uint32, FSoundWavePlaybackTimeData> SoundWavePlaybackTimes;

	/** Restore relative transform from auto attachment and optionally detach from parent (regardless of whether it was an auto attachment). */
	void CancelAutoAttachment(bool bDetachFromParent, const UWorld* MyWorld);

protected:

	/** Utility function called by Play and FadeIn to start a sound playing. */
	void PlayInternal(const PlayInternalRequestData& InPlayRequestData);

#if WITH_EDITORONLY_DATA
	/** Utility function that updates which texture is displayed on the sprite dependent on the properties of the Audio Component. */
	void UpdateSpriteTexture();
#endif

	FRandomStream RandomStream;

	static uint64 AudioComponentIDCounter;
	static TMap<uint64, UAudioComponent*> AudioIDToComponentMap;
	static FCriticalSection AudioIDToComponentMapLock;
};
