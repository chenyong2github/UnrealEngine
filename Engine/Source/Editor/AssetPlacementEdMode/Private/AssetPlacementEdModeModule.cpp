// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementEdModeModule.h"
#include "AssetPlacementEdModeCommands.h"
#include "AssetPlacementEdModeStyle.h"
#include "Modules/ModuleManager.h"
#include "AssetTypeActions_PlacementPalette.h"
#include "AssetToolsModule.h"

#define LOCTEXT_NAMESPACE "AssetPlacementEdMode"

IMPLEMENT_MODULE( FAssetPlacementEdMode, AssetPlacementEdMode );

void FAssetPlacementEdMode::StartupModule()
{
	FAssetPlacementEdModeStyle::Get();
	FAssetPlacementEdModeCommands::Register();

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	PaletteAssetActions = MakeShared<FAssetTypeActions_PlacementPalette>(EAssetTypeCategories::Type::Misc);
	AssetToolsModule.Get().RegisterAssetTypeActions(PaletteAssetActions.ToSharedRef());
}

void FAssetPlacementEdMode::ShutdownModule()
{
	if (FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools"))
	{
		AssetToolsModule->Get().UnregisterAssetTypeActions(PaletteAssetActions.ToSharedRef());
	}
	PaletteAssetActions.Reset();

	FAssetPlacementEdModeCommands::Unregister();
	FAssetPlacementEdModeStyle::Shutdown();
}

#undef LOCTEXT_NAMESPACE
