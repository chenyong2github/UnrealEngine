// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HairStrandsEditor.h"
#include "HairStrandsActions.h"

void FHairStrandsEditor::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TSharedRef<IAssetTypeActions> AssetActions = MakeShareable(new FHairStrandsActions());

	AssetTools.RegisterAssetTypeActions(AssetActions);
	RegisteredAssetTypeActions.Add(AssetActions);
}

void FHairStrandsEditor::ShutdownModule()
{
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

