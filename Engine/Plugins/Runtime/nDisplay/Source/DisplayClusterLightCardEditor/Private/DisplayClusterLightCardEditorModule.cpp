// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightCardEditorModule.h"

#include "SDisplayClusterLightCardEditor.h"

#define LOCTEXT_NAMESPACE "DisplayClusterLightCardEditor"

void FDisplayClusterLightCardEditorModule::StartupModule()
{
	RegisterTabSpawners();
}

void FDisplayClusterLightCardEditorModule::ShutdownModule()
{
	UnregisterTabSpawners();
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
