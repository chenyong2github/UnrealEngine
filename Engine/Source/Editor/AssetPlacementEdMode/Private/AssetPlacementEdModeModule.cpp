// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementEdModeModule.h"
#include "AssetPlacementEdModeCommands.h"
#include "AssetPlacementEdModeStyle.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "AssetPlacementEdMode"

IMPLEMENT_MODULE( FAssetPlacementEdMode, AssetPlacementEdMode );

void FAssetPlacementEdMode::StartupModule()
{
	FAssetPlacementEdModeStyle::Initialize();
	FAssetPlacementEdModeCommands::Register();
}

void FAssetPlacementEdMode::ShutdownModule()
{
	FAssetPlacementEdModeCommands::Unregister();
	FAssetPlacementEdModeStyle::Shutdown();
}

#undef LOCTEXT_NAMESPACE
