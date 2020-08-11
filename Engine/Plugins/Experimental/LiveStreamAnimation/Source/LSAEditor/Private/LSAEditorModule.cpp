// Copyright Epic Games, Inc. All Rights Reserved.

#include "LSAEditorModule.h"
#include "LiveLink/LSALiveLinkFrameTranslatorAssetActions.h"
#include "LSAHandleDetailCustomization.h"
#include "IAssetTools.h"

IMPLEMENT_MODULE(FLSAEditorModule, LSAEditor)

static const FName AssetToolsModuleName(TEXT("AssetTools"));
static const FName PropertyEditorModuleName(TEXT("PropertyEditor"));

void FLSAEditorModule::StartupModule()
{
	TSharedRef<IAssetTypeActions> LocalFrameTranslatorActions = MakeShared<FLSALiveLinkFrameTranslatorAssetActions>();

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(AssetToolsModuleName).Get();
	AssetTools.RegisterAssetTypeActions(LocalFrameTranslatorActions);
	FrameTranslatorActions = LocalFrameTranslatorActions;

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(PropertyEditorModuleName);
	PropertyModule.RegisterCustomPropertyTypeLayout(FLiveStreamAnimationHandleWrapper::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(FLSAHandleDetailCustomization::MakeInstance));

}

void FLSAEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded(AssetToolsModuleName))
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(AssetToolsModuleName).Get();
		AssetTools.UnregisterAssetTypeActions(FrameTranslatorActions.ToSharedRef());
	}

	if (FModuleManager::Get().IsModuleLoaded(PropertyEditorModuleName))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(PropertyEditorModuleName);
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLiveStreamAnimationHandleWrapper::StaticStruct()->GetFName());
	}

	FrameTranslatorActions.Reset();
}