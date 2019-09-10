// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioMixer.h"
#include "AudioMixerPlatformAndroid.h"
#include "Modules/ModuleManager.h"

class FAudioMixerModuleAndroid : public IAudioDeviceModule
{
public:
	virtual bool IsAudioMixerModule() const override { return true; }

	virtual Audio::IAudioMixerPlatformInterface* CreateAudioMixerPlatformInterface() override
	{
		return new Audio::FMixerPlatformAndroid();
	}
};

IMPLEMENT_MODULE(FAudioMixerModuleAndroid, AudioMixerAndroid);
