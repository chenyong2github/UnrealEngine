// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commands/RenderPagesEditorCommands.h"

#define LOCTEXT_NAMESPACE "FRenderPagesEditor"


void UE::RenderPages::Private::FRenderPagesEditorCommands::RegisterCommands()
{
	UI_COMMAND(AddPage, "Add", "Adds a new page instance to the list.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CopyPage, "Copy", "Adds a new page instance to the list by copying from an existing instance.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(DeletePage, "Delete", "Deletes an existing page instance from the list.", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(BatchRenderList, "Render", "Renders the available page instance(s) in batch.", EUserInterfaceActionType::Button, FInputChord());
}


#undef LOCTEXT_NAMESPACE
