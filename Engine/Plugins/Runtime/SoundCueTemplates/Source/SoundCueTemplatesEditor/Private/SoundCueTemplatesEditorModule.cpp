// Copyright Epic Games, Inc. All Rights Reserved.

#include "SoundCueTemplatesEditorModule.h"

#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"

#include "AssetTypeActions_SoundCueTemplate.h"

void FSoundCueTemplatesEditorModule::StartupModule()
{
	RegisterAssetActions();
}

void FSoundCueTemplatesEditorModule::ShutdownModule()
{
}

void FSoundCueTemplatesEditorModule::RegisterAssetActions()
{
	// Register the audio editor asset type actions
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	AssetTools.RegisterAssetTypeActions(MakeShared<FAssetTypeActions_SoundCueTemplate>());
}

IMPLEMENT_MODULE(FSoundCueTemplatesEditorModule, SoundCueTemplatesEditor);
