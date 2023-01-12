// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ChaosClothAssetEditorModule.h"
#include "ChaosClothAsset/ClothEditorMode.h"
#include "ChaosClothAsset/ClothEditorCommands.h"
#include "ChaosClothAsset/ClothEditorStyle.h"
#include "ChaosClothAsset/ClothDataflowNodes.h"
#include "ContentBrowserMenuContexts.h"
#include "EditorModeRegistry.h"
#include "Selection.h"
#include "ToolMenus.h"
#include "Editor.h"
#include "Styling/AppStyle.h"
#include "AssetToolsModule.h"
#include "AssetTypeActions_ClothAsset.h"

#define LOCTEXT_NAMESPACE "FChaosClothAssetEditorModule"

using namespace UE::Chaos::ClothAsset;

void FChaosClothAssetEditorModule::StartupModule()
{
	FChaosClothAssetEditorStyle::Get(); // Causes the constructor to be called

	FChaosClothAssetEditorCommands::Register();

	// Register asset actions
	FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
	IAssetTools& AssetTools = AssetToolsModule.Get();

	AssetTypeActions_ClothAsset = new FAssetTypeActions_ClothAsset();
	AssetTools.RegisterAssetTypeActions(MakeShareable(AssetTypeActions_ClothAsset));

	Dataflow::RegisterClothDataflowNodes();

	// TODO: Register details view customizations
}

void FChaosClothAssetEditorModule::ShutdownModule()
{
	FChaosClothAssetEditorCommands::Unregister();

	FEditorModeRegistry::Get().UnregisterMode(UChaosClothAssetEditorMode::EM_ChaosClothAssetEditorModeId);

	if (UObjectInitialized())
	{
		// Unregister asset actions
		FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
		IAssetTools& AssetTools = AssetToolsModule.Get();
		AssetTools.UnregisterAssetTypeActions(AssetTypeActions_ClothAsset->AsShared());
	}

	// TODO: Unregister details view customizations
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FChaosClothAssetEditorModule, ChaosClothAssetEditor)
