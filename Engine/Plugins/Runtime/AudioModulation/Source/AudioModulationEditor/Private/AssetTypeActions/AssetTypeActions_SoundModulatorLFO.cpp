// Copyright Epic Games, Inc. All Rights Reserved.
#include "AssetTypeActions_SoundModulatorLFO.h"

#include "SoundModulatorLFO.h"


#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_SoundModulatorLFO::GetSupportedClass() const
{
	return USoundBusModulatorLFO::StaticClass();
}

const TArray<FText>& FAssetTypeActions_SoundModulatorLFO::GetSubMenus() const
{
	static const TArray<FText> SubMenus
	{
		FText(LOCTEXT("AssetSoundMixSubMenu", "Mix"))
	};

	return SubMenus;
}
#undef LOCTEXT_NAMESPACE