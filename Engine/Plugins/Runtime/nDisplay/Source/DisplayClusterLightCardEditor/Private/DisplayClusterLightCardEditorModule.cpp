// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightCardEditorModule.h"

#include "DisplayClusterLightCardEditorCommands.h"
#include "SDisplayClusterLightCardEditor.h"

#define LOCTEXT_NAMESPACE "DisplayClusterLightCardEditor"

void FDisplayClusterLightCardEditorModule::StartupModule()
{
	RegisterTabSpawners();

	FDisplayClusterLightCardEditorCommands::Register();
}

void FDisplayClusterLightCardEditorModule::ShutdownModule()
{
	UnregisterTabSpawners();

	FDisplayClusterLightCardEditorCommands::Unregister();
}

void FDisplayClusterLightCardEditorModule::RegisterTabSpawners()
{
	SDisplayClusterLightCardEditor::RegisterTabSpawner();
}

void FDisplayClusterLightCardEditorModule::UnregisterTabSpawners()
{
	SDisplayClusterLightCardEditor::UnregisterTabSpawner();
}

IMPLEMENT_MODULE(FDisplayClusterLightCardEditorModule, DisplayClusterLightCardEditor);

#undef LOCTEXT_NAMESPACE
