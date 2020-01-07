// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryModeModule.h"
#include "Modules/ModuleManager.h"
#include "EditorModeManager.h"
#include "Editor.h"
#include "EditorModeRegistry.h"
#include "GeometryEdMode.h"


FEditorModeID FGeometryEditingModes::EM_Geometry = FEditorModeID(TEXT("EM_Geometry"));
FEditorModeID FGeometryEditingModes::EM_Bsp = FEditorModeID(TEXT("EM_Bsp"));
FEditorModeID FGeometryEditingModes::EM_TextureAlign = FEditorModeID(TEXT("EM_TextureAlign"));

void FGeometryModeModule::StartupModule()
{
	FEditorModeRegistry::Get().RegisterMode<FEdModeGeometry>(
		FGeometryEditingModes::EM_Geometry,
		NSLOCTEXT("EditorModes", "GeometryMode", "Geometry Editing"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.BspMode", "LevelEditor.BspMode.Small"),
		true, 500
	);
}


void FGeometryModeModule::ShutdownModule()
{
	FEditorModeRegistry::Get().UnregisterMode(FGeometryEditingModes::EM_Geometry);
}

IMPLEMENT_MODULE(FGeometryModeModule, GeometryMode);
