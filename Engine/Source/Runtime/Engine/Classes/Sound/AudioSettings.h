// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPath.h"
#include "Engine/DeveloperSettings.h"
#include "AudioMixerTypes.h"
#include "AudioSettings.generated.h"

struct FPropertyChangedChainEvent;

// Enumeration for what our options are for sample rates used for VOIP.
UENUM()
enum class EVoiceSampleRate : int32
{
	Low16000Hz = 16000,
	Normal24000Hz = 24000,
	/* High48000Hz = 48000 //TODO: 48k VOIP requires serious performance optimizations on encoding and decoding. */
};

// Enumeration defines what method of panning to use (for non-binaural audio) with the audio-mixer.
UENUM()
enum class EPanningMethod : int8
{
	// Linear panning maintains linear amplitude when panning between speakers.
	Linear,

	// Equal power panning maintains equal power when panning between speakers.
	EqualPower
};

// Enumeration defines how to treat mono 2D playback. Mono sounds need to upmixed to stereo when played back in 2D.
UENUM()
enum class EMonoChannelUpmixMethod : int8
{
	// The mono channel is split 0.5 left/right
	Linear,

	// The mono channel is split 0.707 left/right
	EqualPower,

	// The mono channel is split 1.0 left/right
	FullVolume
};

USTRUCT()
struct ENGINE_API FAudioQualitySettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Quality")
	FText DisplayName;

	// The number of audio channels that can be used at once
	// NOTE: Some platforms may cap this value to a lower setting regardless of what the settings request
	UPROPERTY(EditAnywhere, Category="Quality", meta=(ClampMin="1"))
	int32 MaxChannels;

	FAudioQualitySettings()
		: MaxChannels(32)
	{
	}
};

/**
 * Audio settings.
 */
UCLASS(config=Engine, defaultconfig, meta=(DisplayName="Audio"))
class ENGINE_API UAudioSettings : public UDeveloperSettings
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
	virtual void PreEditChange(UProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;

	/** Event to listen for when settings reflected properties are changed. */
	DECLARE_EVENT(UAudioSettings, FAudioSettingsChanged)
#endif // WITH_EDITOR

	virtual void Serialize(FArchive& Ar) override;

	/** The SoundClass assigned to newly created sounds */
	UPROPERTY(config, EditAnywhere, Category="Audio", meta=(AllowedClasses="SoundClass", DisplayName="Default Sound Class"))
	FSoftObjectPath DefaultSoundClassName;

	/** The SoundClass assigned to media player assets */
	UPROPERTY(config, EditAnywhere, Category = "Audio", meta = (AllowedClasses = "SoundClass", DisplayName = "Default Media Sound Class"))
	FSoftObjectPath DefaultMediaSoundClassName;

	/** The SoundConcurrency assigned to newly created sounds */
	UPROPERTY(config, EditAnywhere, Category = "Audio", meta = (AllowedClasses = "SoundConcurrency", DisplayName = "Default Sound Concurrency"))
	FSoftObjectPath DefaultSoundConcurrencyName;

	/** The SoundMix to use as base when no other system has specified a Base SoundMix */
	UPROPERTY(config, EditAnywhere, Category="Audio", meta=(AllowedClasses="SoundMix"))
	FSoftObjectPath DefaultBaseSoundMix;

	/** Sound class to be used for the VOIP audio component */
	UPROPERTY(config, EditAnywhere, Category="Audio", meta=(AllowedClasses="SoundClass", DisplayName = "VOIP Sound Class"))
	FSoftObjectPath VoiPSoundClass;

	/** Sample rate used for voice over IP. VOIP audio is resampled to the application's sample rate on the receiver side. */
	UPROPERTY(config, EditAnywhere, Category = "Audio", meta = (DisplayName = "VOIP Sample Rate"))
	EVoiceSampleRate VoiPSampleRate;

	/** The amount of audio to send to reverb submixes if no reverb send is setup for the source through attenuation settings. Only used in audio mixer. */
	UPROPERTY(config)
	float DefaultReverbSendLevel_DEPRECATED;

	/** Enables legacy version of reverb. The legacy reverb runs more slowly, but by most other measures is functionally equivalent. It has a slight perceptual difference. */
	UPROPERTY(config, EditAnywhere, Category = "Audio", AdvancedDisplay)
	uint32 bEnableLegacyReverb: 1;

	/** How many streaming sounds can be played at the same time (if more are played they will be sorted by priority) */
	UPROPERTY(config, EditAnywhere, Category="Audio", meta=(ClampMin=0))
	int32 MaximumConcurrentStreams;

	/** The value to use to clamp the min pitch scale */
	UPROPERTY(config, EditAnywhere, Category = "Audio", meta = (ClampMin = 0.001, ClampMax = 4.0, UIMin = 0.001, UIMax = 4.0))
	float GlobalMinPitchScale;

	/** The value to use to clamp the min pitch scale */
	UPROPERTY(config, EditAnywhere, Category = "Audio", meta = (ClampMin = 0.001, ClampMax = 4.0, UIMin = 0.001, UIMax = 4.0))
	float GlobalMaxPitchScale;

	UPROPERTY(config, EditAnywhere, Category="Quality")
	TArray<FAudioQualitySettings> QualityLevels;

	/** Allows sounds to play at 0 volume. */
	UPROPERTY(config, EditAnywhere, Category = "Quality", AdvancedDisplay)
	uint32 bAllowPlayWhenSilent:1;

	/** Disables master EQ effect in the audio DSP graph. */
	UPROPERTY(config, EditAnywhere, Category = "Quality", AdvancedDisplay)
	uint32 bDisableMasterEQ : 1;

	/** Enables the surround sound spatialization calculations to include the center channel. */
	UPROPERTY(config, EditAnywhere, Category = "Quality", AdvancedDisplay)
	uint32 bAllowCenterChannel3DPanning : 1;

	/**
	 * The max number of sources to reserve for "stopping" sounds. A "stopping" sound applies a fast fade in the DSP
	 * render to prevent discontinuities when stopping sources.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Quality", AdvancedDisplay)
	uint32 NumStoppingSources;

	/**
	* The method to use when doing non-binaural or object-based panning.
	*/
	UPROPERTY(config, EditAnywhere, Category = "Quality", AdvancedDisplay)
	EPanningMethod PanningMethod;

	/**
	* The upmixing method for mono sound sources. Defines up mono channels are up-mixed to stereo channels.
	*/
	UPROPERTY(config, EditAnywhere, Category = "Quality", AdvancedDisplay)
	EMonoChannelUpmixMethod MonoChannelUpmixMethod;

	/**
	 * The format string to use when generating the filename for contexts within dialogue waves. This must generate unique names for your project.
	 * Available format markers:
	 *   * {DialogueGuid} - The GUID of the dialogue wave. Guaranteed to be unique and stable against asset renames.
	 *   * {DialogueHash} - The hash of the dialogue wave. Not guaranteed to be unique or stable against asset renames, however may be unique enough if combined with the dialogue name.
	 *   * {DialogueName} - The name of the dialogue wave. Not guaranteed to be unique or stable against asset renames, however may be unique enough if combined with the dialogue hash.
	 *   * {ContextId}    - The ID of the context. Guaranteed to be unique within its dialogue wave. Not guaranteed to be stable against changes to the context.
	 *   * {ContextIndex} - The index of the context within its parent dialogue wave. Guaranteed to be unique within its dialogue wave. Not guaranteed to be stable against contexts being removed.
	 */
	UPROPERTY(config, EditAnywhere, Category="Dialogue")
	FString DialogueFilenameFormat;

#if WITH_EDITOR
	FAudioSettingsChanged AudioSettingsChanged;
#endif // WITH_EDITOR

public:
	// Get the quality level settings at the provided level index
	const FAudioQualitySettings& GetQualityLevelSettings(int32 QualityLevel) const;
	
	// Get the quality name level for a given index
	FString FindQualityNameByIndex(int32 Index) const;

	// Get the total number of quality level settings
	int32 GetQualityLevelSettingsNum() const;

	// Sets whether audio mixer is enabled. Set once an audio mixer platform module is loaded.
	void SetAudioMixerEnabled(const bool bInAudioMixerEnabled);

	// Returns if the audio mixer is currently enabled
	const bool IsAudioMixerEnabled() const;

	/** Returns the highest value for MaxChannels among all quality levels */
	int32 GetHighestMaxChannels() const;

#if WITH_EDITOR
	/** Returns event to be bound to if caller wants to know when audio settings are modified */
	FAudioSettingsChanged& OnAudioSettingsChanged() { return AudioSettingsChanged; }
#endif // WITH_EDITOR

private:
#if WITH_EDITOR
	TArray<FAudioQualitySettings> CachedQualityLevels;
#endif // WITH_EDITOR

	void AddDefaultSettings();

	// Whether or not the audio mixer is loaded/enabled. Used to toggle visibility of editor features.
	bool bIsAudioMixerEnabled;
};
