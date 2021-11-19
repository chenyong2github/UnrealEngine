// Copyright Epic Games, Inc. All Rights Reserved.

#include "CurveTableEditorCommands.h"

#define LOCTEXT_NAMESPACE "CurveTableEditorCommands"

void FCurveTableEditorCommands::RegisterCommands()
{
	UI_COMMAND(CurveViewToggle, "Curve View", "Changes the view of the curve table from grid to curve view.", EUserInterfaceActionType::ToggleButton, FInputChord());
	UI_COMMAND(AppendKeyColumn, "Append Key Column", "Append a new column to the curve table.\nEvery Curve or Table Row will have a new key appended.", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
