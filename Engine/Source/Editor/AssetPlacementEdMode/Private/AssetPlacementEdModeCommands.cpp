// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetPlacementEdModeCommands.h"
#include "AssetPlacementEdModeStyle.h"

#define LOCTEXT_NAMESPACE "FAssetPlacementEdMode"

FAssetPlacementEdModeCommands::FAssetPlacementEdModeCommands()
	: TCommands<FAssetPlacementEdModeCommands>("AssetPlacementEdMode",
		NSLOCTEXT("AssetPlacementEdMode", "AssetPlacementEdModeCommands", "Sample Tools Editor Mode"),
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
	UI_COMMAND(SelectInvalid, "Invalid", "Select Invalid Assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(SelectInvalid);
	UI_COMMAND(LassoSelect, "Lasso", "Select Assets with Lasso", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(LassoSelect);
	UI_COMMAND(Paint, "Paint", "Paint Assets  from the Palette", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(Paint);
	UI_COMMAND(Reapply, "Reapply", "Reapply Settings to Assets from the Palette", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(Reapply);
	UI_COMMAND(PlaceSingle, "Single", "Place a Single Asset from the Palette", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(PlaceSingle);
	UI_COMMAND(Fill, "Fill", "Fill the Selected Target with Assets from the Palette", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(Fill);
	UI_COMMAND(Erase, "Erase", "Erases Assets Matching the Palette", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(Erase);
	UI_COMMAND(Delete, "Delete", "Remove the Selected Assets", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(Delete);
	UI_COMMAND(MoveToCurrentLevel, "Move", "Move Selected Assets to the Current Level", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(MoveToCurrentLevel);
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> FAssetPlacementEdModeCommands::GetCommands()
{
	return FAssetPlacementEdModeCommands::Get().Commands;
}

#undef LOCTEXT_NAMESPACE
