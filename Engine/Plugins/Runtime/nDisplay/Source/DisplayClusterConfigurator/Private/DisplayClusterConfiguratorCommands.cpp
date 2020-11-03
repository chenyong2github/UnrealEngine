// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorCommands.h"

#define LOCTEXT_NAMESPACE "DisplayClusterConfiguratorCommands"


void FDisplayClusterConfiguratorCommands::RegisterCommands()
{
	UI_COMMAND(Import, "Import", "Import nDisplay Config", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SaveToFile, "SaveToFile", "Save With Open File Dialog", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(EditConfig, "EditConfig", "Edit config file with text editor", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ToggleWindowInfo, "Show Window Info", "Enables or disables showing the window information", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::W));
	UI_COMMAND(ToggleWindowCornerImage, "Show Window Corner Image", "Enables or disables showing the window corner image", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::E));
	UI_COMMAND(ToggleOutsideViewports, "Show Viewports outside the Window", "Enables or disables showing the viewport which is compleatly outside window", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::R));
	UI_COMMAND(ZoomToFit, "Zoom To Fit", "Zoom To Fit In Graph", EUserInterfaceActionType::Button, FInputChord(EKeys::Z));

	UI_COMMAND(BrowseDocumentation, "Documentation", "Opens the documentation reference documentation", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
