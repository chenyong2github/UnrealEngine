// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HairStrandsEditor.h"
#include "GroomActions.h"

#include "FbxHairTranslator.h"

IMPLEMENT_MODULE(FHairStrandsEditor, HairStrandsEditor);

void FHairStrandsEditor::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TSharedRef<IAssetTypeActions> AssetActions = MakeShareable(new FGroomActions());

	AssetTools.RegisterAssetTypeActions(AssetActions);
	RegisteredAssetTypeActions.Add(AssetActions);

	RegisterHairTranslator<FFbxHairTranslator>();
}

void FHairStrandsEditor::ShutdownModule()
{
	// #ueent_todo: Unregister the translators
	FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");

	if (AssetToolsModule != nullptr)
	{
		IAssetTools& AssetTools = AssetToolsModule->Get();

		for (auto Action : RegisteredAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(Action);
		}
	}
}

TArray<TSharedPtr<IHairStrandsTranslator>> FHairStrandsEditor::GetHairTranslators()
{
	TArray<TSharedPtr<IHairStrandsTranslator>> Translators;
	for (TFunction<TSharedPtr<IHairStrandsTranslator>()>& SpawnTranslator : TranslatorSpawners)
	{
		Translators.Add(SpawnTranslator());
	}

	return Translators;
}
