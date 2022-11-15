// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinitions/AssetDefinition_NiagaraEffectType.h"

#include "NiagaraEditorStyle.h"
#include "NiagaraEffectType.h"

#define LOCTEXT_NAMESPACE "UAssetDefinition_NiagaraEffectType"

FLinearColor UAssetDefinition_NiagaraEffectType::GetAssetColor() const
{
	return FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.AssetColors.EffectType");
}

TSoftClassPtr<> UAssetDefinition_NiagaraEffectType::GetAssetClass() const
{
	return UNiagaraEffectType::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_NiagaraEffectType::GetAssetCategories() const
{
	static FAssetCategoryPath AssetPaths[] = { FAssetCategoryPath(LOCTEXT("NiagaraAssetsCategory", "FX"), LOCTEXT("NiagaraEffectType_SubCategory", "Advanced")) };
	return AssetPaths;
}

#undef LOCTEXT_NAMESPACE
