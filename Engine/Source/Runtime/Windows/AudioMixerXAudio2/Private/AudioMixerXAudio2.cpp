// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioMixerPlatformXAudio2.h"
#include "AudioMixer.h"
#include "Modules/ModuleManager.h"

class FAudioMixerModuleXAudio2 : public IAudioDeviceModule
{
public:
	virtual bool IsAudioMixerModule() const override 
	{ 
		return true;
	}

	virtual Audio::IAudioMixerPlatformInterface* CreateAudioMixerPlatformInterface() override
	{
		return new Audio::FMixerPlatformXAudio2();
	}
};

IMPLEMENT_MODULE(FAudioMixerModuleXAudio2, AudioMixerXAudio2);
