// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementEdModeCommands.h"
#include "AssetPlacementEdModeStyle.h"

#define LOCTEXT_NAMESPACE "AssetPlacementEdMode"

FAssetPlacementEdModeCommands::FAssetPlacementEdModeCommands()
	: TCommands<FAssetPlacementEdModeCommands>("AssetPlacementEdMode",
		LOCTEXT("AssetPlacementEdModeCommands", "AssetPlacement Editor Mode Commands"),
		NAME_None,
		FAssetPlacementEdModeStyle::Get().GetStyleSetName())
{
}

void FAssetPlacementEdModeCommands::RegisterCommands()
{
	TArray <TSharedPtr<FUICommandInfo>>& ToolCommands = Commands.FindOrAdd(NAME_Default);
	UI_COMMAND(Select, "Select", "Select by clicking single assets matching the active palette.", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(Select);
	UI_COMMAND(LassoSelect, "Lasso", "Selects asset by painting the area to select.", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(LassoSelect);
	UI_COMMAND(Place, "Paint", "Paint mutliple assets from the active palette.", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(Place);
	UI_COMMAND(PlaceSingle, "Single", "Place a single, random asset from the active palette.", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(PlaceSingle);
	UI_COMMAND(Erase, "Erase", "Paint to erase assets matching the active palette.", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(Erase);
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> FAssetPlacementEdModeCommands::GetCommands()
{
	return FAssetPlacementEdModeCommands::Get().Commands;
}

#undef LOCTEXT_NAMESPACE
