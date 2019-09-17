// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_SoundControlBus.h"
#include "SoundControlBus.h"


UClass* FAssetTypeActions_SoundVolumeControlBus::GetSupportedClass() const
{
	return USoundVolumeControlBus::StaticClass();
}

UClass* FAssetTypeActions_SoundPitchControlBus::GetSupportedClass() const
{
	return USoundPitchControlBus::StaticClass();
}

UClass* FAssetTypeActions_SoundLPFControlBus::GetSupportedClass() const
{
	return USoundLPFControlBus::StaticClass();
}

UClass* FAssetTypeActions_SoundHPFControlBus::GetSupportedClass() const
{
	return USoundHPFControlBus::StaticClass();
}
