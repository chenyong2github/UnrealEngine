// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_SoundModulatorLFO.h"
#include "SoundModulatorLFO.h"


UClass* FAssetTypeActions_SoundModulatorLFO::GetSupportedClass() const
{
	return USoundModulatorLFO::StaticClass();
}
