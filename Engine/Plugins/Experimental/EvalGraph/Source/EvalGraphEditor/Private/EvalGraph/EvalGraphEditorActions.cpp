// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraph/EvalGraphEditorActions.h"

#define LOCTEXT_NAMESPACE "EvalGraphEditorCommands"

void FEvalGraphEditorCommandsImpl::RegisterCommands()
{
	UI_COMMAND(EvaluateNode, "Evaluate", "Trigger an evaluation of the selected node.", EUserInterfaceActionType::Button, FInputChord())
}

void FEvalGraphEditorCommands::Register()
{
	return FEvalGraphEditorCommandsImpl::Register();
}

const FEvalGraphEditorCommandsImpl& FEvalGraphEditorCommands::Get()
{
	return FEvalGraphEditorCommandsImpl::Get();
}

void FEvalGraphEditorCommands::Unregister()
{
	return FEvalGraphEditorCommandsImpl::Unregister();
}

#undef LOCTEXT_NAMESPACE
