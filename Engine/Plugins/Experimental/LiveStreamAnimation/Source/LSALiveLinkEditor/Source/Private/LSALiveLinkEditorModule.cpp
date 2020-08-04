// Copyright Epic Games, Inc. All Rights Reserved.

#include "LSALiveLinkEditorModule.h"
#include "LSALiveLinkFrameTranslatorAssetActions.h"
#include "IAssetTools.h"

IMPLEMENT_MODULE(FLSALiveLinkEditorModule, LSALiveLinkEditor)

static const FName AssetToolsModuleName(TEXT("AssetTools"));

void FLSALiveLinkEditorModule::StartupModule()
{
	TSharedRef<IAssetTypeActions> LocalFrameTranslatorActions = MakeShared<FLSALiveLinkFrameTranslatorAssetActions>();

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(AssetToolsModuleName).Get();
	AssetTools.RegisterAssetTypeActions(LocalFrameTranslatorActions);
	FrameTranslatorActions = LocalFrameTranslatorActions;
}

void FLSALiveLinkEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded(AssetToolsModuleName))
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(AssetToolsModuleName).Get();
		AssetTools.UnregisterAssetTypeActions(FrameTranslatorActions.ToSharedRef());
	}

	FrameTranslatorActions.Reset();
}