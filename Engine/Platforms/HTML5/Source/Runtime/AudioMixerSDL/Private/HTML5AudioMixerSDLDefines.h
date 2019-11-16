// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMixerPlatformSDL.h"

namespace Audio
{
	class FAudioMixerPlatformSDL : public FMixerPlatformSDL
	{
public:
		SDL_AudioFormat GetPlatformAudioFormat() { return AUDIO_S16; }
		Uint8 GetPlatformChannels() { return 2; }
		EAudioMixerStreamDataFormat::Type GetAudioStreamFormat() { return EAudioMixerStreamDataFormat::Int16; }
		int32 GetAudioStreamChannelSize() { return sizeof(int16); }
	};
}
