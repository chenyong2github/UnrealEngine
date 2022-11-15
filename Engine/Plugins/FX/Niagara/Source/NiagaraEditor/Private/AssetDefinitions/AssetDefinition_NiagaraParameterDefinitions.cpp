// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinitions/AssetDefinition_NiagaraParameterDefinitions.h"

#include "NiagaraEditorStyle.h"
#include "NiagaraParameterDefinitions.h"
#include "NiagaraParameterDefinitionsToolkit.h"

#define LOCTEXT_NAMESPACE "UAssetDefinition_NiagaraParameterDefinitions"

FLinearColor UAssetDefinition_NiagaraParameterDefinitions::GetAssetColor() const
{
	return FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.AssetColors.ParameterDefinitions");
}

TSoftClassPtr<> UAssetDefinition_NiagaraParameterDefinitions::GetAssetClass() const
{
	return UNiagaraParameterDefinitions::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_NiagaraParameterDefinitions::GetAssetCategories() const
{
	static FAssetCategoryPath AssetPaths[] = { FAssetCategoryPath(LOCTEXT("NiagaraAssetsCategory", "FX"), LOCTEXT("NiagaraParameterDefinitions_SubCategory", "Advanced")) };
	return AssetPaths;
}

EAssetCommandResult UAssetDefinition_NiagaraParameterDefinitions::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	if (OpenArgs.OpenMethod == EAssetOpenMethod::Edit)
	{
		EToolkitMode::Type Mode = OpenArgs.ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;
		TArray<UObject*> Objects = OpenArgs.LoadObjects<UObject>();
		for (UObject* OpenObj : Objects)
		{
			if (UNiagaraParameterDefinitions* ParameterDefinitions = Cast<UNiagaraParameterDefinitions>(OpenObj))
			{
				TSharedRef< FNiagaraParameterDefinitionsToolkit > NewNiagaraParameterDefinitionsToolkit(new FNiagaraParameterDefinitionsToolkit());
				NewNiagaraParameterDefinitionsToolkit->Initialize(Mode, OpenArgs.ToolkitHost, ParameterDefinitions);
			}
		}
		return EAssetCommandResult::Handled;
	}

	return EAssetCommandResult::Unhandled;
}

#undef LOCTEXT_NAMESPACE
