// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IAudioExtensionPlugin.h"
#include "OVR_Audio.h"
#include "Sound/SoundEffectBase.h"
#include "Sound/SoundEffectSubmix.h"
#include "Templates/UniquePtr.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "AudioDeviceManager.h"

#include "OculusAudioReverb.generated.h"


// Forward Declarations
class USubmixEffectOculusReverbPluginPreset;

// data used to initialize the Oculus Reverb Submix Effect.
struct FOculusReverbSubmixInitData
{
	// The length of each buffer callback, in frames.
	int32 BufferLength;
	// The maximum number of sources we expect to render.
	int32 MaxNumSources;
	// The sample rate of the incoming and outgoing audio stream.
	float SampleRate;
};

class FSubmixEffectOculusReverbPlugin : public FSoundEffectSubmix
{
	virtual void Init(const FSoundEffectSubmixInitData& InInitData) override;

	virtual uint32 GetDesiredInputChannelCountOverride() const override
	{
		static const int STEREO = 2;
		return STEREO; // PAS
	}
	virtual void OnProcessAudio(const FSoundEffectSubmixInputData& InData, FSoundEffectSubmixOutputData& OutData) override;
	virtual void OnPresetChanged() override
	{
		return; // PAS
	}

public:
	FSubmixEffectOculusReverbPlugin();

	void ClearContext();

	// This extra initialization step must be called after Init to ensure we've properly setup the shared context
	// between the reverb and spatialization plugins. It's only required because this fix is being done in a hotfix
	// and we don't want to change any public headers- in the future we can simply add this data to FSoundEffectSubmixInitData.
	void InitializeContext(const FOculusReverbSubmixInitData& InContextInitData);

private:
	ovrAudioContext Context;
	FCriticalSection ContextLock;

	Audio::FDeviceId OwningDeviceId;
};

/************************************************************************/
/* OculusAudioReverb													*/
/* This implementation of IAudioReverb uses the Oculus Audio			*/
/* library to render spatial reverb.									*/
/************************************************************************/
class OculusAudioReverb : public IAudioReverb
{
public:
	OculusAudioReverb();

	void ClearContext();

	virtual void OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, const uint32 NumChannels, UReverbPluginSourceSettingsBase* InSettings) override
	{
		return; // PAS
	}

	virtual void OnReleaseSource(const uint32 SourceId) override 
	{
		return; // PAS
	}

	virtual FSoundEffectSubmixPtr GetEffectSubmix() override;

	virtual USoundSubmix* GetSubmix() override;

	virtual void ProcessSourceAudio(const FAudioPluginSourceInputData& InputData, FAudioPluginSourceOutputData& OutputData) override
	{
		return; // PAS
	}

	void Initialize(const FAudioPluginInitializationParams InitializationParams) override;

private:
	ovrAudioContext* Context;
	TSoundEffectSubmixPtr SubmixEffect;
	USubmixEffectOculusReverbPluginPreset* ReverbPreset;

	int32 BufferLength;
	int32 MaxNumSources;
	float SampleRate;
};

USTRUCT()
struct OCULUSAUDIO_API FSubmixEffectOculusReverbPluginSettings
{
	GENERATED_USTRUCT_BODY()

	FSubmixEffectOculusReverbPluginSettings() = default;
};

UCLASS()
class OCULUSAUDIO_API USubmixEffectOculusReverbPluginPreset : public USoundEffectSubmixPreset
{
	GENERATED_BODY()

public:
	EFFECT_PRESET_METHODS(SubmixEffectOculusReverbPlugin)

	UFUNCTION()
	void SetSettings(const FSubmixEffectOculusReverbPluginSettings& InSettings)
	{
	}

	UPROPERTY()
	FSubmixEffectOculusReverbPluginSettings Settings;
};