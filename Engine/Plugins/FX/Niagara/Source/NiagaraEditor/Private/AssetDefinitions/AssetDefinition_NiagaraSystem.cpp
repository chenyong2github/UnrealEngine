// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinitions/AssetDefinition_NiagaraSystem.h"

#include "NiagaraEditorStyle.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemToolkit.h"

#define LOCTEXT_NAMESPACE "UAssetDefinition_NiagaraSystem"

FLinearColor UAssetDefinition_NiagaraSystem::GetAssetColor() const
{
	return FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.AssetColors.System");
}

TSoftClassPtr<> UAssetDefinition_NiagaraSystem::GetAssetClass() const
{
	return UNiagaraSystem::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_NiagaraSystem::GetAssetCategories() const
{
	static FAssetCategoryPath AssetPaths[] = { LOCTEXT("NiagaraAssetsCategory", "FX"), EAssetCategoryPaths::Basic };
	return AssetPaths;
}

EAssetCommandResult UAssetDefinition_NiagaraSystem::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	if (OpenArgs.OpenMethod == EAssetOpenMethod::Edit)
	{
		EToolkitMode::Type Mode = OpenArgs.ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;
		TArray<UObject*> Objects = OpenArgs.LoadObjects<UObject>();
		for (UObject* OpenObj : Objects)
		{
			if (UNiagaraSystem* System = Cast<UNiagaraSystem>(OpenObj))
			{
				TSharedRef<FNiagaraSystemToolkit> NewNiagaraSystemToolkit(new FNiagaraSystemToolkit());
				NewNiagaraSystemToolkit->InitializeWithSystem(Mode, OpenArgs.ToolkitHost, *System);
			}
		}
		return EAssetCommandResult::Handled;
	}

	return EAssetCommandResult::Unhandled;
}

#undef LOCTEXT_NAMESPACE
