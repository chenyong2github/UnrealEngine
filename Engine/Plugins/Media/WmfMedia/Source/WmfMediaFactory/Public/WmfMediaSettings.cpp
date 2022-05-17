// Copyright Epic Games, Inc. All Rights Reserved.

#include "WmfMediaSettings.h"

UWmfMediaSettings::UWmfMediaSettings()
	: AllowNonStandardCodecs(false)
	, LowLatency(false)
	, NativeAudioOut(false)
	, HardwareAcceleratedVideoDecoding(false)
	, bAreHardwareAcceleratedCodecRegistered(false)
{ }

void UWmfMediaSettings::EnableHardwareAcceleratedCodecRegistered()
{
	bAreHardwareAcceleratedCodecRegistered = true;
}
