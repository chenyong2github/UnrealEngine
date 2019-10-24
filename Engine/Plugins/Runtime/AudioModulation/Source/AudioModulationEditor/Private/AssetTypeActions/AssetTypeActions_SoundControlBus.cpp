// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "AssetTypeActions_SoundControlBus.h"

#include "SoundControlBus.h"


#define LOCTEXT_NAMESPACE "AssetTypeActions"

namespace Modulation
{
	static const TArray<FText> SubMenus
	{
		FText(LOCTEXT("AssetSoundMixSubMenu", "Mix"))
	};
} // namespace <>

UClass* FAssetTypeActions_SoundVolumeControlBus::GetSupportedClass() const
{
	return USoundVolumeControlBus::StaticClass();
}

const TArray<FText>& FAssetTypeActions_SoundVolumeControlBus::GetSubMenus() const
{
	return Modulation::SubMenus;
}

UClass* FAssetTypeActions_SoundPitchControlBus::GetSupportedClass() const
{
	return USoundPitchControlBus::StaticClass();
}

const TArray<FText>& FAssetTypeActions_SoundPitchControlBus::GetSubMenus() const
{
	return Modulation::SubMenus;
}

UClass* FAssetTypeActions_SoundLPFControlBus::GetSupportedClass() const
{
	return USoundLPFControlBus::StaticClass();
}

const TArray<FText>& FAssetTypeActions_SoundLPFControlBus::GetSubMenus() const
{
	return Modulation::SubMenus;
}

UClass* FAssetTypeActions_SoundHPFControlBus::GetSupportedClass() const
{
	return USoundHPFControlBus::StaticClass();
}

const TArray<FText>& FAssetTypeActions_SoundHPFControlBus::GetSubMenus() const
{
	return Modulation::SubMenus;
}

UClass* FAssetTypeActions_SoundControlBus::GetSupportedClass() const
{
	return USoundControlBus::StaticClass();
}

const TArray<FText>& FAssetTypeActions_SoundControlBus::GetSubMenus() const
{
	return Modulation::SubMenus;
}
#undef LOCTEXT_NAMESPACE
