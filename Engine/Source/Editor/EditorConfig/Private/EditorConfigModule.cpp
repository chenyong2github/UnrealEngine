// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorConfigModule.h"
#include "EditorConfig.h"
#include "EditorConfigSubsystem.h"
#include "EditorMetadataOverrides.h"
#include "Misc/Paths.h"

void FEditorConfigModule::StartupModule()
{
}

void FEditorConfigModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FEditorConfigModule, EditorConfig)