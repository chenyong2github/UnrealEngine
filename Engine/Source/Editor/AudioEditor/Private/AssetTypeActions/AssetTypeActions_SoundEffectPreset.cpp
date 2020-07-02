// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_SoundEffectPreset.h"
#include "Developer/AssetTools/Public/IAssetTools.h"
#include "Developer/AssetTools/Public/AssetToolsModule.h"
#include "Editors/SoundEffectPresetEditor.h"
#include "HAL/IConsoleManager.h"
#include "Settings/EditorLoadingSavingSettings.h"
#include "Toolkits/IToolkitHost.h"
#include "Toolkits/SimpleAssetEditor.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

static bool bPrototypeSFXEditorEnabled = false;
static FAutoConsoleCommand GEnableSoundEffectEditorPrototype(
	TEXT("au.AudioEditor.EnableSoundEffectEditorPrototype"),
	TEXT("Enables's the UE5 prototype sound effect editor.\n"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			bPrototypeSFXEditorEnabled = true;
		}
	)
);

namespace EffectPresets
{
	static const TArray<FText> SubMenus
	{
		FText(LOCTEXT("AssetEffectSubMenu", "Effects"))
	};
} // namespace EffectPresets

FAssetTypeActions_SoundEffectPreset::FAssetTypeActions_SoundEffectPreset(USoundEffectPreset* InEffectPreset)
	: EffectPreset(InEffectPreset)
{
}

FText FAssetTypeActions_SoundEffectPreset::GetName() const
{
	FText AssetActionName = EffectPreset->GetAssetActionName();
	if (AssetActionName.IsEmpty())
	{
		FString ClassName;
		EffectPreset->GetClass()->GetName(ClassName);
		ensureMsgf(false, TEXT("U%sGetAssetActionName not implemented. Please check that EFFECT_PRESET_METHODS(EffectClassName) is at the top of the declaration of %s."), *ClassName, *ClassName);
		FString DefaultName = ClassName + FString(TEXT(" (Error: EFFECT_PRESET_METHODS() Not Used in Class Declaration)"));
		return FText::FromString(DefaultName);
	}
	else
	{
		return EffectPreset->GetAssetActionName();
	}
}

UClass* FAssetTypeActions_SoundEffectPreset::GetSupportedClass() const
{
	UClass* SupportedClass = EffectPreset->GetSupportedClass();
	if (SupportedClass == nullptr)
	{
		FString ClassName;
		EffectPreset->GetClass()->GetName(ClassName);
		ensureMsgf(false, TEXT("U%s::GetSupportedClass not implemented. Please check that EFFECT_PRESET_METHODS(EffectClassName) is at the top of the declaration of %s."), *ClassName, *ClassName);
		return EffectPreset->GetClass();
	}
	else
	{
		return SupportedClass;
	}
}

const TArray<FText>& FAssetTypeActions_SoundEffectSubmixPreset::GetSubMenus() const
{
	return EffectPresets::SubMenus;
}

const TArray<FText>& FAssetTypeActions_SoundEffectPreset::GetSubMenus() const
{
	return EffectPresets::SubMenus;
}

const TArray<FText>& FAssetTypeActions_SoundEffectSourcePresetChain::GetSubMenus() const
{
	return EffectPresets::SubMenus;
}

const TArray<FText>& FAssetTypeActions_SoundEffectSourcePreset::GetSubMenus() const
{
	return EffectPresets::SubMenus;
}

void FAssetTypeActions_SoundEffectPreset::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> ToolkitHost)
{
	if (!bPrototypeSFXEditorEnabled)
	{
		FAssetTypeActions_Base::OpenAssetEditor(InObjects, ToolkitHost);
	}
	else
	{
		EToolkitMode::Type Mode = ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

		for (UObject* Object : InObjects)
		{
			if (USoundEffectPreset* Preset = Cast<USoundEffectPreset>(Object))
			{
				TSharedRef<FSoundEffectPresetEditor> PresetEditor = MakeShared<FSoundEffectPresetEditor>();
				PresetEditor->Init(Mode, ToolkitHost, Preset);
			}
		}
	}
}
#undef LOCTEXT_NAMESPACE