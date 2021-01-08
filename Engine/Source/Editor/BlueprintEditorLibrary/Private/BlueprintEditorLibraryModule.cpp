// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintEditorLibraryModule.h"

#define LOCTEXT_NAMESPACE "FBlueprintEditorLibraryModule"

void FBlueprintEditorLibraryModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	UE_LOG(LogTemp, Warning, TEXT("BP Editor Lib Loaded"));
}

void FBlueprintEditorLibraryModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.	
	UE_LOG(LogTemp, Warning, TEXT("BP Editor Lib Loaded"));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlueprintEditorLibraryModule, BlueprintEditorLibrary)