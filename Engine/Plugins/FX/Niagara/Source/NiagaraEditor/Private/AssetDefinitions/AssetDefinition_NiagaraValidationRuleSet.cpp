// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinitions/AssetDefinition_NiagaraValidationRuleSet.h"

#include "NiagaraEditorStyle.h"
#include "NiagaraValidationRuleSet.h"

#define LOCTEXT_NAMESPACE "UAssetDefinition_NiagaraValidationRuleSet"

FLinearColor UAssetDefinition_NiagaraValidationRuleSet::GetAssetColor() const
{
	return FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.AssetColors.ValidationRuleSet");
}

TSoftClassPtr<> UAssetDefinition_NiagaraValidationRuleSet::GetAssetClass() const
{
	return UNiagaraValidationRuleSet::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_NiagaraValidationRuleSet::GetAssetCategories() const
{
	static FAssetCategoryPath AssetPaths[] = { FAssetCategoryPath(LOCTEXT("NiagaraAssetsCategory", "FX"), LOCTEXT("NiagaraValidationRuleSet_SubCategory", "Advanced")) };
	return AssetPaths;
}

#undef LOCTEXT_NAMESPACE
