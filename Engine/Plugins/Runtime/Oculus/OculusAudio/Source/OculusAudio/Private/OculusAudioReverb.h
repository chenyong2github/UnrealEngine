// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IAudioExtensionPlugin.h"
#include "Sound/SoundEffectSubmix.h"
#include "OVR_Audio.h"
#include "Templates/UniquePtr.h"


class FSubmixEffectOculusReverbPlugin : public FSoundEffectSubmix
{
public:
	FSubmixEffectOculusReverbPlugin();

	void SetContext(ovrAudioContext* SharedContext);
	void ClearContext();

	virtual void Init(const FSoundEffectSubmixInitData& InSampleRate) override
	{
		return; // PAS
	}
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
private:
	ovrAudioContext* Context;
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
	{
		// empty
	}

	void SetContext(ovrAudioContext* SharedContext);
	void ClearContext();

	virtual void OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, const uint32 NumChannels, UReverbPluginSourceSettingsBase* InSettings) override
	{
		return; // PAS
	}

	virtual void OnReleaseSource(const uint32 SourceId) override 
	{
		return; // PAS
	}

	virtual TSharedPtr<FSoundEffectSubmix> GetEffectSubmix() override;

	virtual USoundSubmix* GetSubmix() override;

	virtual void ProcessSourceAudio(const FAudioPluginSourceInputData& InputData, FAudioPluginSourceOutputData& OutputData) override
	{
		return; // PAS
	}
private:
	ovrAudioContext* Context;
	TSharedPtr<FSubmixEffectOculusReverbPlugin> Submix;
};