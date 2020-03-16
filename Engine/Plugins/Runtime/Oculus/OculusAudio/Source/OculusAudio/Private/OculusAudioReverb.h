// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IAudioExtensionPlugin.h"
#include "OVR_Audio.h"
#include "Sound/SoundEffectBase.h"
#include "Sound/SoundEffectSubmix.h"
#include "Templates/UniquePtr.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "OculusAudioReverb.generated.h"


UCLASS()
class USubmixEffectOculusReverbPreset : public USoundEffectSubmixPreset
{
	GENERATED_BODY()

public:
	EFFECT_PRESET_METHODS(SubmixEffectOculusReverbPreset)
};

class FSubmixEffectOculusReverbPlugin : public FSoundEffectSubmix
{
	void ClearContext();

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

protected:
	FSubmixEffectOculusReverbPlugin();

private:
	ovrAudioContext Context;
	FCriticalSection ContextLock;
};

/************************************************************************/
/* OculusAudioReverb													*/
/* This implementation of IAudioReverb uses the Oculus Audio			*/
/* library to render spatial reverb.									*/
/************************************************************************/
class OculusAudioReverb : public IAudioReverb
{
public:
	OculusAudioReverb()
		: Context(nullptr)
		, ReverbPreset(nullptr)
	{
		// empty
	}

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
private:
	ovrAudioContext* Context;
	TSoundEffectSubmixPtr SubmixEffect;
	USubmixEffectOculusReverbPreset* ReverbPreset;
};