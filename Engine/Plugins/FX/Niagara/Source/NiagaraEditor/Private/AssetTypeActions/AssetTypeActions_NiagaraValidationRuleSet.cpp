// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_NiagaraValidationRuleSet.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraValidationRuleSet.h"

FText FAssetTypeActions_NiagaraValidationRuleSet::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_NiagaraValidationRuleSet", "Niagara Validation Rule Set");
}

FColor FAssetTypeActions_NiagaraValidationRuleSet::GetTypeColor() const
{
	return FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.AssetColors.ValidationRuleSet").ToFColor(true);
}

UClass* FAssetTypeActions_NiagaraValidationRuleSet::GetSupportedClass() const
{
	return UNiagaraValidationRuleSet::StaticClass();
}

const TArray<FText>& FAssetTypeActions_NiagaraValidationRuleSet::GetSubMenus() const
{
	static const TArray<FText> SubMenus
	{
		NSLOCTEXT("Niagara", "NiagaraAssetSubMenu_Advanced", "Advanced")
	};
	return SubMenus;
}
