// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementEdModeCommands.h"
#include "AssetPlacementEdModeStyle.h"

#define LOCTEXT_NAMESPACE "AssetPlacementEdMode"

FAssetPlacementEdModeCommands::FAssetPlacementEdModeCommands()
	: TCommands<FAssetPlacementEdModeCommands>("AssetPlacementEdMode",
		LOCTEXT("AssetPlacementEdModeCommands", "AssetPlacement Editor Mode Commands"),
		NAME_None,
		FAssetPlacementEdModeStyle::GetStyleSetName())
{
}

void FAssetPlacementEdModeCommands::RegisterCommands()
{
	TArray <TSharedPtr<FUICommandInfo>>& ToolCommands = Commands.FindOrAdd(NAME_Default);
	UI_COMMAND(Select, "Select", "Select", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(Select);
	UI_COMMAND(SelectAll, "All", "Select All Assets from the Palette", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(SelectAll);
	UI_COMMAND(Deselect, "Deselect", "Deselect All Assets from the Palette", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(Deselect);
	UI_COMMAND(LassoSelect, "Lasso", "Select Assets with Lasso", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(LassoSelect);
	UI_COMMAND(Place, "Place", "Place Assets from the Palette", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(Place);
	UI_COMMAND(PlaceSingle, "Single", "Place a Single Asset from the Palette", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(PlaceSingle);
	UI_COMMAND(Fill, "Fill", "Fill the Selected Target with Assets from the Palette", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(Fill);
	UI_COMMAND(Erase, "Erase", "Erases Assets Matching the Palette", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(Erase);
	UI_COMMAND(Delete, "Delete", "Remove the Selected Assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(Delete);
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> FAssetPlacementEdModeCommands::GetCommands()
{
	return FAssetPlacementEdModeCommands::Get().Commands;
}

#undef LOCTEXT_NAMESPACE
