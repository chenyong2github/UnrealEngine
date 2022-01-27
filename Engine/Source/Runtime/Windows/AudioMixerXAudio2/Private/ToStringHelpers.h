// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMixer.h"

#if PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"

THIRD_PARTY_INCLUDES_START
#include <mmdeviceapi.h>			// IMMNotificationClient
#include <audiopolicy.h>			// IAudioSessionEvents
THIRD_PARTY_INCLUDES_END

#include "Windows/HideWindowsPlatformTypes.h"

#endif //PLATFORM_WINDOWS

namespace Audio
{
#if PLATFORM_WINDOWS
	const TCHAR* ToString(AudioSessionDisconnectReason InDisconnectReason);
	const TCHAR* ToString(ERole InRole);
	const TCHAR* ToString(EDataFlow InFlow);
	FString ToFString(const PROPERTYKEY Key);
#endif //PLATFORM_WINDOWS

	FString ToFString(const TArray<EAudioMixerChannel::Type>& InChannels);
	FString ToErrorFString(HRESULT InResult);

	const TCHAR* ToString(EAudioDeviceRole InRole);
	const TCHAR* ToString(EAudioDeviceState InState);
}
