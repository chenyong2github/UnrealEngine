// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/HierarchyEditor/NiagaraHierarchyCommands.h"

#define LOCTEXT_NAMESPACE "NiagaraHierarchyEditorCommands"

void FNiagaraHierarchyEditorCommands::RegisterCommands()
{
	UI_COMMAND(Delete, "Delete", "Delete current selection", EUserInterfaceActionType::Button, FInputChord(EKeys::Delete))
}

#undef LOCTEXT_NAMESPACE
