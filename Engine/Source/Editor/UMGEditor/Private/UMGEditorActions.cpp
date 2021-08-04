// Copyright Epic Games, Inc. All Rights Reserved.

#include "UMGEditorActions.h"

#define LOCTEXT_NAMESPACE "UMGEditorCommands"

void FUMGEditorCommands::RegisterCommands()
{
	UI_COMMAND(CreateNativeBaseClass, "Create Native Base Class", "Create a native base class for this widget, using the current parent as the parent of the native class.", EUserInterfaceActionType::Button, FInputChord() );
	UI_COMMAND(ExportAsPNG, "Export as Image", "Export the current widget blueprint as .png format.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(SetImageAsThumbnail, "Capture and Set as Thumbnail", "Captures the current state of the widget blueprint and sets it as the thumbnail for this asset.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ClearCustomThumbnail, "Clear Thumbnail", "Removes the image used as thumbnail and enables automatic thumbnail generation.", EUserInterfaceActionType::Button, FInputChord());

}

#undef LOCTEXT_NAMESPACE