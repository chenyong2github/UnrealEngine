// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_SoundEffectPreset.h"
#include "Developer/AssetTools/Public/IAssetTools.h"
#include "Developer/AssetTools/Public/AssetToolsModule.h"
#include "Settings/EditorLoadingSavingSettings.h"
#include "Toolkits/IToolkitHost.h"

#include "Toolkits/SimpleAssetEditor.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

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

#undef LOCTEXT_NAMESPACE