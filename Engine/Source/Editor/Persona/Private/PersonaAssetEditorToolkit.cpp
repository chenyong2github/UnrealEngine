// Copyright Epic Games, Inc. All Rights Reserved.

#include "PersonaAssetEditorToolkit.h"

#include "PersonaModule.h"
#include "AssetEditorModeManager.h"
#include "Modules/ModuleManager.h"
#include "IPersonaEditorModeManager.h"

void FPersonaAssetEditorToolkit::CreateEditorModeManager()
{
	TSharedPtr<FAssetEditorModeManager> ModeManager = MakeShareable(FModuleManager::LoadModuleChecked<FPersonaModule>("Persona").CreatePersonaEditorModeManager());
	
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	SetAssetEditorModeManager(ModeManager.Get());
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}
