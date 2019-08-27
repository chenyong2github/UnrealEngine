// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_SoundModulatorBusMix.h"
#include "SoundModulatorBusMix.h"


UClass* FAssetTypeActions_SoundModulatorBusMix::GetSupportedClass() const
{
	return USoundModulatorBusMix::StaticClass();
}
