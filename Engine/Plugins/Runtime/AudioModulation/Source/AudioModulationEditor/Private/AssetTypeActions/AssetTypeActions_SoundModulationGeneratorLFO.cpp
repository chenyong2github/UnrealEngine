// Copyright Epic Games, Inc. All Rights Reserved.
#include "AssetTypeActions_SoundModulationGeneratorLFO.h"

#include "SoundModulationGeneratorLFO.h"


#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_SoundModulationGeneratorLFO::GetSupportedClass() const
{
	return USoundModulationGeneratorLFO::StaticClass();
}

const TArray<FText>& FAssetTypeActions_SoundModulationGeneratorLFO::GetSubMenus() const
{
	static const TArray<FText> SubMenus
	{
		LOCTEXT("AssetSoundModulationSubMenu", "Modulation")
	};

	return SubMenus;
}
#undef LOCTEXT_NAMESPACE