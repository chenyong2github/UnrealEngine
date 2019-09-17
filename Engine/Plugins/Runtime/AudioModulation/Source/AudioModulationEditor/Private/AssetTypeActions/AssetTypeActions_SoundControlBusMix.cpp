// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_SoundControlBusMix.h"
#include "SoundControlBusMix.h"


UClass* FAssetTypeActions_SoundControlBusMix::GetSupportedClass() const
{
	return USoundControlBusMix::StaticClass();
}
