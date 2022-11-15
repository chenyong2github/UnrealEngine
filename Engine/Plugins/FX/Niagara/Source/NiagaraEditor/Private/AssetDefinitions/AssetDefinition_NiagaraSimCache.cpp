// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinitions/AssetDefinition_NiagaraSimCache.h"

#include "NiagaraEditorStyle.h"
#include "NiagaraSimCache.h"
#include "NiagaraSimCacheToolkit.h"

#define LOCTEXT_NAMESPACE "UAssetDefinition_NiagaraSimCache"

FLinearColor UAssetDefinition_NiagaraSimCache::GetAssetColor() const
{
	return FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.AssetColors.SimCache");
}

TSoftClassPtr<> UAssetDefinition_NiagaraSimCache::GetAssetClass() const
{
	return UNiagaraSimCache::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_NiagaraSimCache::GetAssetCategories() const
{
	static FAssetCategoryPath AssetPaths[] = { FAssetCategoryPath(LOCTEXT("NiagaraAssetsCategory", "FX"), LOCTEXT("NiagaraSimCache_SubCategory", "Advanced")) };
	return AssetPaths;
}

EAssetCommandResult UAssetDefinition_NiagaraSimCache::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	if (OpenArgs.OpenMethod == EAssetOpenMethod::Edit)
	{
		EToolkitMode::Type Mode = OpenArgs.ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;
		TArray<UObject*> Objects = OpenArgs.LoadObjects<UObject>();
		for (UObject* OpenObj : Objects)
		{
			if (UNiagaraSimCache* Cache = Cast<UNiagaraSimCache>(OpenObj))
			{
				TSharedRef<FNiagaraSimCacheToolkit> NewNiagaraSystemToolkit(new FNiagaraSimCacheToolkit());
				NewNiagaraSystemToolkit->Initialize(Mode, OpenArgs.ToolkitHost, Cache);
			}
		}
		return EAssetCommandResult::Handled;
	}

	return EAssetCommandResult::Unhandled;
}

#undef LOCTEXT_NAMESPACE
