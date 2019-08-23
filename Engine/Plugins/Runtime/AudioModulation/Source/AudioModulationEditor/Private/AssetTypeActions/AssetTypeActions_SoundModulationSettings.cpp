// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "AssetTypeActions_SoundModulationSettings.h"

#include "SoundModulationPatch.h"


UClass* FAssetTypeActions_SoundModulationSettings::GetSupportedClass() const
{
	return USoundModulationSettings::StaticClass();
}
