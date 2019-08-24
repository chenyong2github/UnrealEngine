// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_SoundModulatorBus.h"
#include "SoundModulatorBus.h"


UClass* FAssetTypeActions_SoundVolumeModulatorBus::GetSupportedClass() const
{
	return USoundVolumeModulatorBus::StaticClass();
}

UClass* FAssetTypeActions_SoundPitchModulatorBus::GetSupportedClass() const
{
	return USoundPitchModulatorBus::StaticClass();
}

UClass* FAssetTypeActions_SoundLPFModulatorBus::GetSupportedClass() const
{
	return USoundLPFModulatorBus::StaticClass();
}

UClass* FAssetTypeActions_SoundHPFModulatorBus::GetSupportedClass() const
{
	return USoundHPFModulatorBus::StaticClass();
}
