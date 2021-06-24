// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenXREditorModule.h"
#include "OpenXRAssetDirectory.h"

void FOpenXREditorModule::StartupModule()
{
	FOpenXRAssetDirectory::LoadForCook();
}

void FOpenXREditorModule::ShutdownModule()
{
	FOpenXRAssetDirectory::ReleaseAll();
}

IMPLEMENT_MODULE(FOpenXREditorModule, OpenXREditor);
