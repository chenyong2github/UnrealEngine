// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementEdModeToolkit.h"
#include "AssetPlacementEdMode.h"

#include "Modules/ModuleManager.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

#include "EditorModeManager.h"

#define LOCTEXT_NAMESPACE "AssetPlacementEdModeToolkit"

FAssetPlacementEdModeToolkit::FAssetPlacementEdModeToolkit()
{
}

void FAssetPlacementEdModeToolkit::GetToolPaletteNames(TArray<FName>& PaletteNames) const
{
	PaletteNames.Add(NAME_Default);
}


FName FAssetPlacementEdModeToolkit::GetToolkitFName() const
{
	return FName("AssetPlacementEdMode");
}

FText FAssetPlacementEdModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("DisplayName", "AssetPlacementEdMode Tool");
}

#undef LOCTEXT_NAMESPACE
