// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_SoundSubmix.h"
#include "AudioEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Sound/SoundSubmix.h"
#include "SoundSubmixEditor.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

UClass* FAssetTypeActions_SoundSubmix::GetSupportedClass() const
{
	return USoundSubmix::StaticClass();
}

void FAssetTypeActions_SoundSubmix::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (UObject* Obj : InObjects)
	{
		if (USoundSubmix* SoundSubmix = Cast<USoundSubmix>(Obj))
		{
			IAudioEditorModule* AudioEditorModule = &FModuleManager::LoadModuleChecked<IAudioEditorModule>("AudioEditor");
			AudioEditorModule->CreateSoundSubmixEditor(Mode, EditWithinLevelEditor, SoundSubmix);
		}
	}
}

bool FAssetTypeActions_SoundSubmix::AssetsActivatedOverride(const TArray<UObject*>& InObjects, EAssetTypeActivationMethod::Type ActivationType)
{
	TSet<USoundSubmix*> SubmixesToSelect;
	IAssetEditorInstance* Editor = nullptr;
	for (UObject* Obj : InObjects)
	{
		if (USoundSubmix* SubmixToSelect = Cast<USoundSubmix>(Obj))
		{
			if (!Editor)
			{
				Editor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Obj, false);
			}
			SubmixesToSelect.Add(SubmixToSelect);
		}
	}

	if (Editor)
	{
		static_cast<FSoundSubmixEditor*>(Editor)->SelectSubmixes(SubmixesToSelect);
		return true;
	}

	return false;
}

const TArray<FText>& FAssetTypeActions_SoundSubmix::GetSubMenus() const
{
	static const TArray<FText> SubMenus
	{
		LOCTEXT("AssetSoundMixSubMenu", "Mix")
	};

	return SubMenus;
}
#undef LOCTEXT_NAMESPACE