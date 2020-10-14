// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsEditorModule.h"

#include "AssetToolsModule.h"
#include "AssetTypeActions_LevelSnapshot.h"
#include "IAssetTools.h"

#define LOCTEXT_NAMESPACE "FLevelSnapshotsEditorModule"

void FLevelSnapshotsEditorModule::StartupModule()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	AssetTools.RegisterAssetTypeActions(MakeShared<FAssetTypeActions_LevelSnapshot>());
}

void FLevelSnapshotsEditorModule::ShutdownModule()
{

}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FLevelSnapshotsEditorModule, LevelSnapshotsEditor)