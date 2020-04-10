// Copyright Epic Games, Inc. All Rights Reserved.
#include "AssetTypeActions_SoundModulationPatch.h"

#include "Editors/ModulationSettingsEditor.h"
#include "SoundModulationPatch.h"


#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_SoundModulationPatch::GetSupportedClass() const
{
	return USoundModulationPatch::StaticClass();
}

const TArray<FText>& FAssetTypeActions_SoundModulationPatch::GetSubMenus() const
{
	static const TArray<FText> SubMenus
	{
		FText(LOCTEXT("AssetSoundMixSubMenu", "Mix"))
	};

	return SubMenus;
}

#undef LOCTEXT_NAMESPACE
