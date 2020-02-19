// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commands/DMXEditorCommands.h"

#define LOCTEXT_NAMESPACE "DMXEditorCommands"

FDMXEditorCommandsImpl::FDMXEditorCommandsImpl()
	: TCommands<FDMXEditorCommandsImpl>(TEXT("DMXEditor"), LOCTEXT("DMXEditor", "DMX Editor"), NAME_None, FEditorStyle::GetStyleSetName())
{}

void FDMXEditorCommandsImpl::RegisterCommands()
{
	UI_COMMAND(GoToDocumentation, "View Documentation", "View documentation about DMX editor", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(AddNewEntityController, "New Controller", "Creates a new Controller in this library", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(AddNewEntityFixtureType, "New Fixture Type", "Creates a new Fixture Type in this library", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(AddNewEntityFixturePatch, "Add Fixture", "Creates a new Fixture Patch in this library", EUserInterfaceActionType::Button, FInputChord());
}

void FDMXEditorCommands::Register()
{
	return FDMXEditorCommandsImpl::Register();
}

const FDMXEditorCommandsImpl& FDMXEditorCommands::Get()
{
	return FDMXEditorCommandsImpl::Get();
}

void FDMXEditorCommands::Unregister()
{
	return FDMXEditorCommandsImpl::Unregister();
}

#undef LOCTEXT_NAMESPACE
