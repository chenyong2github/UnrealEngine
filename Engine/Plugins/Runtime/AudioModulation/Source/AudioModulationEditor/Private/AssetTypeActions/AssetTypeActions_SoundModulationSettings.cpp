// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "AssetTypeActions_SoundModulationSettings.h"

#include "Editors/ModulationSettingsEditor.h"
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

void FAssetTypeActions_SoundModulationSettings::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> ToolkitHost)
{
	EToolkitMode::Type Mode = ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (UObject* Object : InObjects)
	{
		if (USoundModulationSettings* ModulationSettings = Cast<USoundModulationSettings>(Object))
		{
			TSharedRef<FModulationSettingsEditor> SettingsEditor = MakeShared<FModulationSettingsEditor>();
			SettingsEditor->Init(Mode, ToolkitHost, ModulationSettings);
		}
	}
}
#undef LOCTEXT_NAMESPACE
