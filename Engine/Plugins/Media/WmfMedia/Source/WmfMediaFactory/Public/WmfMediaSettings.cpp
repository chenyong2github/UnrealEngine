// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	if (bAreHardwareAcceleratedCodecRegistered == false)
	{
		bAreHardwareAcceleratedCodecRegistered = true;
		AllowNonStandardCodecs = true;
		HardwareAcceleratedVideoDecoding = true;
#if WITH_EDITOR
		SaveConfig(CPF_Config, *GetDefaultConfigFilename());
#endif
	}
}

#if WITH_EDITOR

bool UWmfMediaSettings::CanEditChange(const UProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty))
	{
		return false;
	}

	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UWmfMediaSettings, AllowNonStandardCodecs) || 
		InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UWmfMediaSettings, HardwareAcceleratedVideoDecoding))
	{
		return !(bAreHardwareAcceleratedCodecRegistered);
	}

	return true;
}

#endif //WITH_EDITOR

