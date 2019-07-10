// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StandardToolModeCommands.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "StandardToolModeCommands"


FStandardToolModeCommands::FStandardToolModeCommands() :
	TCommands<FStandardToolModeCommands>(
		"StandardToolCommands", // Context name for fast lookup
		NSLOCTEXT("Contexts", "StandardToolCommands", "Standard Tool Commands"), // Localized context name for displaying
		NAME_None, // Parent
		FEditorStyle::GetStyleSetName() // Icon Style Set
		)
{}


void FStandardToolModeCommands::RegisterCommands()
{
	TSharedPtr<FUICommandInfo> IncreaseBrushSize;
	UI_COMMAND(IncreaseBrushSize, "Increase Brush Size", "Increases the size of the brush", EUserInterfaceActionType::Button, FInputChord(EKeys::RightBracket));
	Commands.Add(EStandardToolModeCommands::IncreaseBrushSize, IncreaseBrushSize);

	TSharedPtr<FUICommandInfo> DecreaseBrushSize;
	UI_COMMAND(DecreaseBrushSize, "Decrease Brush Size", "Decreases the size of the brush", EUserInterfaceActionType::Button, FInputChord(EKeys::LeftBracket));
	Commands.Add(EStandardToolModeCommands::DecreaseBrushSize, DecreaseBrushSize);
}


TSharedPtr<FUICommandInfo> FStandardToolModeCommands::FindStandardCommand(EStandardToolModeCommands Command) const
{
	const TSharedPtr<FUICommandInfo>* Found = Commands.Find(Command);
	checkf(Found != nullptr, TEXT("FStandardToolModeCommands::FindStandardCommand: standard command %d was not found!"), Command);
	return (Found != nullptr) ? *Found : nullptr;
}


#undef LOCTEXT_NAMESPACE
