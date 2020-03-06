// Copyright Epic Games, Inc. All Rights Reserved.

#include "SoundCueTemplatesEditorModule.h"

#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"
#include "ToolMenus.h"

#include "AssetTypeActions_SoundCueTemplate.h"

void FSoundCueTemplatesEditorModule::StartupModule()
{
	RegisterAssetActions();
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FSoundCueTemplatesEditorModule::RegisterMenus));
}

void FSoundCueTemplatesEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner("SoundCueTemplates");
}

void FSoundCueTemplatesEditorModule::RegisterAssetActions()
{
	// Register the audio editor asset type actions
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	AssetTools.RegisterAssetTypeActions(MakeShared<FAssetTypeActions_SoundCueTemplate>());
}

void FSoundCueTemplatesEditorModule::RegisterMenus()
{
	FAssetActionExtender_SoundCueTemplate::RegisterMenus();
}

IMPLEMENT_MODULE(FSoundCueTemplatesEditorModule, SoundCueTemplatesEditor);
