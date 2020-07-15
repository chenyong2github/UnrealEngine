// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveStreamAnimationEditorModule.h"
#include "AssetTypeActions_LiveStreamAnimationLiveLinkFrameTranslator.h"
#include "IAssetTools.h"

IMPLEMENT_MODULE(FLiveStreamAnimationEditorModule, LiveStreamAnimationEditor)

static const FName AssetToolsModuleName(TEXT("AssetTools"));

void FLiveStreamAnimationEditorModule::StartupModule()
{
	TSharedRef<IAssetTypeActions> LocalFrameTranslatorActions = MakeShared<FAssetTypeActions_LiveStreamAnimationLiveLinkFrameTranslator>();

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(AssetToolsModuleName).Get();
	AssetTools.RegisterAssetTypeActions(LocalFrameTranslatorActions);
	FrameTranslatorActions = LocalFrameTranslatorActions;
}

void FLiveStreamAnimationEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded(AssetToolsModuleName))
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(AssetToolsModuleName).Get();
		AssetTools.UnregisterAssetTypeActions(FrameTranslatorActions.ToSharedRef());
	}

	FrameTranslatorActions.Reset();
}