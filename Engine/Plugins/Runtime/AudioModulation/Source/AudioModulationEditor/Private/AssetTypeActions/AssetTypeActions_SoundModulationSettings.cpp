// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "AssetTypeActions_SoundModulationSettings.h"

#include "SoundModulationPatch.h"


#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_SoundModulationSettings::GetSupportedClass() const
{
	return USoundModulationSettings::StaticClass();
}

const TArray<FText>& FAssetTypeActions_SoundModulationSettings::GetSubMenus() const
{
	static const TArray<FText> SubMenus
	{
		FText(LOCTEXT("AssetSoundMixSubMenu", "Mix"))
	};

	return SubMenus;
}
#undef LOCTEXT_NAMESPACE
