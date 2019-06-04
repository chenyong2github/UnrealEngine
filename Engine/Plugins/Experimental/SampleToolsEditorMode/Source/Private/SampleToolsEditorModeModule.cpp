// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SampleToolsEditorModeModule.h"
#include "SampleToolsEditorMode.h"

#define LOCTEXT_NAMESPACE "FSampleToolsEditorModeModule"

void FSampleToolsEditorModeModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	FEditorModeRegistry::Get().RegisterMode<FSampleToolsEditorMode>(FSampleToolsEditorMode::EM_SampleToolsEditorModeId, LOCTEXT("SampleToolsEditorModeName", "SampleToolsEditorMode"), FSlateIcon(), true);
}

void FSampleToolsEditorModeModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	FEditorModeRegistry::Get().UnregisterMode(FSampleToolsEditorMode::EM_SampleToolsEditorModeId);
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSampleToolsEditorModeModule, SampleToolsEditorMode)