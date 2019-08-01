// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SoundUtilitiesEditorModule.h"
#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"
#include "AssetTypeActions_SoundSimple.h"
#include "AssetTypeActions_Base.h"
#include "EditorMenuSubsystem.h"
#include "AudioEditorModule.h"
#include "SoundWaveAssetActionExtender.h"

IMPLEMENT_MODULE(FSoundUtilitiesEditorModule, SoundUtilitiesEditor)

void FSoundUtilitiesEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	
	// Register asset actions
	AssetTools.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_SoundSimple));

	UEditorMenuSubsystem::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FSoundUtilitiesEditorModule::RegisterMenus));
}

void FSoundUtilitiesEditorModule::ShutdownModule()
{
	UEditorMenuSubsystem::UnRegisterStartupCallback(this);
	UEditorMenuSubsystem::UnregisterOwner("SoundUtilities");
}

void FSoundUtilitiesEditorModule::RegisterMenus()
{
	FSoundWaveAssetActionExtender::RegisterMenus();
}
