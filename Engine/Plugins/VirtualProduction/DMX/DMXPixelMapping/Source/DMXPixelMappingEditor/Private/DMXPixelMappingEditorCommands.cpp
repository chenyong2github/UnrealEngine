// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXPixelMappingEditorCommands.h"

#include "EditorStyleSet.h"
#include "Framework/Commands/Commands.h"

#define LOCTEXT_NAMESPACE "DMXPixelMappingEditorCommands"

FDMXPixelMappingEditorCommands::FDMXPixelMappingEditorCommands()
	: TCommands<FDMXPixelMappingEditorCommands>
	(
		TEXT("DMXPixelMappingEditor"),
		NSLOCTEXT("Contexts", "DMXPixelMappingEditor", "DMX Pixel Mapping"),
		NAME_None,
		FEditorStyle::GetStyleSetName()
	)
{
}

void FDMXPixelMappingEditorCommands::RegisterCommands()
{
	UI_COMMAND(SaveThumbnailImage, "Thumbnail", "Generate Thumbnail", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(AddMapping, "Add Mapping", "Add Mapping", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(PlayDMX, "Play DMX", "Play DMX.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(StopPlayingDMX, "Stop Playing DMX", "Stop Playing DMX.", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(bTogglePlayDMXAll, "Toggle Play DMX All", "Toggles playing all render components and all their childs", EUserInterfaceActionType::ToggleButton, FInputChord());
}

#undef LOCTEXT_NAMESPACE 
