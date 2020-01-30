// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

/**
 * The base class for a playable sound object
 */

#include "Audio.h"
#include "CoreMinimal.h"
#include "IAudioExtensionPlugin.h"
#include "SoundConcurrency.h"
#include "SoundSourceBusSend.h"
#include "SoundSubmixSend.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "SoundBase.generated.h"


class USoundEffectSourcePreset;
class USoundSourceBus;
class USoundSubmix;
class USoundEffectSourcePresetChain;

struct FActiveSound;
struct FSoundParseParameters;

/**
 * Method of virtualization when a sound is stopped due to playback constraints
 * (i.e. by concurrency, priority, and/or MaxChannelCount)
 * for a given sound.
 */
UENUM(BlueprintType)
enum class EVirtualizationMode : uint8
{
	/** Virtualization is disabled */
	Disabled,

	/** Sound continues to play when silent and not virtualize, continuing to use a voice. If
	 * sound is looping and stopped due to concurrency or channel limit/priority, sound will
	 * restart on realization. If any SoundWave referenced in a SoundCue's waveplayer is set
	 * to 'PlayWhenSilent', entire SoundCue will be overridden to 'PlayWhenSilent' (to maintain
	 * timing over all wave players).
	 */
	PlayWhenSilent,

	/** If sound is looping, sound restarts from beginning upon realization from being virtual */
	Restart
};

UCLASS(config=Engine, hidecategories=Object, abstract, editinlinenew, BlueprintType)
class ENGINE_API USoundBase : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	static USoundClass* DefaultSoundClassObject;
	static USoundConcurrency* DefaultSoundConcurrencyObject;

	/** Sound class this sound belongs to */
	UPROPERTY(EditAnywhere, Category = Sound, meta = (DisplayName = "Class"), AssetRegistrySearchable)
	USoundClass* SoundClassObject;

	/** When "stat sounds -debug" has been specified, draw this sound's attenuation shape when the sound is audible. For debugging purpose only. */
	UPROPERTY(EditAnywhere, Category = Debug)
	uint8 bDebug : 1;

	/** Whether or not to override the sound concurrency object with local concurrency settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Management|Concurrency")
	uint8 bOverrideConcurrency : 1;

	/** Whether or not to only send this audio's output to a bus. If true, will not be this sound won't be audible except through bus sends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Source")
	uint8 bOutputToBusOnly : 1;

	/** Whether or not to only send this audio's output to a bus. If true, will not be this sound won't be audible except through bus sends. */
	UPROPERTY()
	uint8 bHasDelayNode : 1;

	/** Whether or not this sound has a concatenator node. If it does, we have to allow the sound to persist even though it may not have generate audible audio in a given audio thread frame. */
	UPROPERTY()
	uint8 bHasConcatenatorNode : 1;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	uint8 bHasVirtualizeWhenSilent_DEPRECATED : 1;
#endif // WITH_EDITORONLY_DATA

	/** Bypass volume weighting priority upon evaluating whether sound should remain active when max channel count is met (See platform Audio Settings). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Management|Priority")
	uint8 bBypassVolumeScaleForPriority : 1;

	/** Virtualization behavior, determining if a sound may revive and how it continues playing when culled or evicted (limited to looping sounds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Management")
	EVirtualizationMode VirtualizationMode;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TEnumAsByte<EMaxConcurrentResolutionRule::Type> MaxConcurrentResolutionRule_DEPRECATED;
#endif // WITH_EDITORONLY_DATA

	/** Map of device handle to number of times this sound is currently being played using that device(counted if sound is virtualized). */
	TMap<Audio::FDeviceId, int32> CurrentPlayCount;

#if WITH_EDITORONLY_DATA
	/** If Override Concurrency is false, the sound concurrency settings to use for this sound. */
	UPROPERTY()
	USoundConcurrency* SoundConcurrencySettings_DEPRECATED;
#endif // WITH_EDITORONLY_DATA

	/** Set of concurrency settings to observe (if override is set to false).  Sound must pass all concurrency settings to play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Management|Concurrency", meta = (EditCondition = "!bOverrideConcurrency"))
	TSet<USoundConcurrency*> ConcurrencySet;

	/** If Override Concurrency is true, concurrency settings to use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Management|Concurrency", meta = (EditCondition = "bOverrideConcurrency"))
	FSoundConcurrencySettings ConcurrencyOverrides;

#if WITH_EDITORONLY_DATA
	/** Maximum number of times this sound can be played concurrently. */
	UPROPERTY()
	int32 MaxConcurrentPlayCount_DEPRECATED;
#endif

	/** Duration of sound in seconds. */
	UPROPERTY(Category=Info, AssetRegistrySearchable, VisibleAnywhere, BlueprintReadOnly)
	float Duration;

	/** The max distance of the asset, as determined by attenuation settings. */
	UPROPERTY(Category = Info, AssetRegistrySearchable, VisibleAnywhere, BlueprintReadOnly)
	float MaxDistance;

	/** Total number of samples (in the thousands). Useful as a metric to analyze the relative size of a given sound asset in content browser. */
	UPROPERTY(Category = Info, AssetRegistrySearchable, VisibleAnywhere, BlueprintReadOnly)
	float TotalSamples;

	/** Used to determine whether sound can play or remain active if channel limit is met, where higher value is higher priority
	  * (see platform's Audio Settings 'Max Channels' property). Unless bypassed, value is weighted with the final volume of the
	  * sound to produce final runtime priority value.
	  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice Management|Priority", meta = (ClampMin = "0.0", UIMin = "0.0", ClampMax = "100.0", UIMax = "100.0"))
	float Priority;

	/** Attenuation settings package for the sound */
	UPROPERTY(EditAnywhere, Category = Attenuation)
	USoundAttenuation* AttenuationSettings;

	/** Modulation for the sound */
	UPROPERTY(EditAnywhere, Category = Modulation)
	FSoundModulation Modulation;

	/** Submix to route sound output to. If unset, falls back to referenced SoundClass submix.
	  * If SoundClass submix is unset, sends to the 'Master Submix' as set in the 'Audio' category of Project Settings'. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Submix", meta = (DisplayName = "Submix"))
	USoundSubmix* SoundSubmixObject;

	/** Array of submix sends to which a prescribed amount (see 'Send Level') of this sound is sent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Submix", meta = (DisplayName = "Submix Sends"))
	TArray<FSoundSubmixSendInfo> SoundSubmixSends;

	/** The source effect chain to use for this sound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Source")
	USoundEffectSourcePresetChain* SourceEffectChain;

	/** This sound will send its audio output to this list of buses if there are bus instances playing after source effects are processed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Source", meta = (DisplayName = "Post-Effect Bus Sends"))
	TArray<FSoundSourceBusSendInfo> BusSends;

	/** This sound will send its audio output to this list of buses if there are bus instances playing before source effects are processed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects|Source", meta = (DisplayName = "Pre-Effect Bus Sends"))
	TArray<FSoundSourceBusSendInfo> PreEffectBusSends;

public:

	//~ Begin UObject Interface.
	virtual void PostInitProperties() override;
#if WITH_EDITORONLY_DATA
	virtual void PostLoad() override;
#endif
	virtual bool CanBeClusterRoot() const override;
	virtual bool CanBeInCluster() const override;
	virtual void Serialize(FArchive& Ar) override;

	//~ End UObject interface.

	/** Returns whether the sound base is set up in a playable manner */
	virtual bool IsPlayable() const;

	/** Returns whether sound supports subtitles. */
	virtual bool SupportsSubtitles() const;

	/** Returns whether or not this sound base has an attenuation node. */
	virtual bool HasAttenuationNode() const;

	/** Returns a pointer to the attenuation settings that are to be applied for this node */
	virtual const FSoundAttenuationSettings* GetAttenuationSettingsToApply() const;

	/**
	 * Returns the farthest distance at which the sound could be heard
	 */
	virtual float GetMaxDistance() const;

	/**
	 * Returns the length of the sound
	 */
	virtual float GetDuration();

	/** Returns whether or not this sound has a delay node, which means it's possible for the sound to not generate audio for a while. */
	bool HasDelayNode() const;

	/** Returns whether or not this sound has a sequencer node, which means it's possible for the owning active sound to persist even though it's not generating audio. */
	bool HasConcatenatorNode() const;

	/** Returns true if any of the sounds in the sound have "play when silent" enabled. */
	virtual bool IsPlayWhenSilent() const;

	virtual float GetVolumeMultiplier();
	virtual float GetPitchMultiplier();

	/** Returns the subtitle priority */
	virtual float GetSubtitlePriority() const { return DEFAULT_SUBTITLE_PRIORITY; };

	/** Returns whether or not any part of this sound wants interior volumes applied to it */
	virtual bool ShouldApplyInteriorVolumes();

	/** Returns curves associated with this sound if it has any. By default returns nullptr, but types
	*	supporting curves can return a corresponding curve table.
	*/
	virtual class UCurveTable* GetCurveData() const { return nullptr; }

	/** Returns whether or not this sound is looping. */
	bool IsLooping();

	/** Parses the Sound to generate the WaveInstances to play. */
	virtual void Parse( class FAudioDevice* AudioDevice, const UPTRINT NodeWaveInstanceHash, FActiveSound& ActiveSound, const FSoundParseParameters& ParseParams, TArray<FWaveInstance*>& WaveInstances ) { }

	/** Returns the SoundClass used for this sound. */
	virtual USoundClass* GetSoundClass() const;

	/** Returns the SoundSubmix used for this sound. */
	virtual USoundSubmix* GetSoundSubmix() const;

	/** Returns the sound submix sends for this sound. */
	void GetSoundSubmixSends(TArray<FSoundSubmixSendInfo>& OutSends) const;

	/** Returns the sound source sends for this sound. */
	void GetSoundSourceBusSends(EBusSendType BusSendType, TArray<FSoundSourceBusSendInfo>& OutSends) const;

	/** Returns an array of FSoundConcurrencySettings handles. */
	void GetConcurrencyHandles(TArray<FConcurrencyHandle>& OutConcurrencyHandles) const;

	/** Returns the priority to use when evaluating concurrency. */
	float GetPriority() const;
	/** Returns whether the sound has cooked analysis data (e.g. FFT or envelope following data) and returns sound waves which have cooked data. */
	virtual bool GetSoundWavesWithCookedAnalysisData(TArray<USoundWave*>& OutSoundWaves);

	/** Queries if the sound has cooked FFT or envelope data. */
	virtual bool HasCookedFFTData() const { return false; }
	virtual bool HasCookedAmplitudeEnvelopeData() const { return false; }
};

