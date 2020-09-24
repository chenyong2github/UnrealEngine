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

	UI_COMMAND(AddNewFixtureTypeMode, "Add Mode", "Creates a new Mode in the Fixture Type", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(AddNewModeFunction, "Add Function", "Creates a new Function in the Mode", EUserInterfaceActionType::Button, FInputChord());

	UI_COMMAND(OpenChannelsMonitor, "Open Channel Monitor", "Open the Monitor for all DMX Channels in a Universe", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OpenActivityMonitor, "Open Activity Monitor", "Open the Monitor for all DMX activity in a range of Universes", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OpenOutputConsole, "Open Output Console", "Open the Console to generate and output DMX Signals", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ToggleReceiveDMX, "Toggle receive DMX", "Sets whether DMX is received in editor irregardless of Project Settings", EUserInterfaceActionType::Button, FInputChord());
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
