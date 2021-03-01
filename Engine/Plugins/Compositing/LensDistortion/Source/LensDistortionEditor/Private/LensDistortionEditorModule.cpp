// Copyright Epic Games, Inc. All Rights Reserved.


#include "LensDistortionEditorModule.h"

#include "AssetToolsModule.h"
#include "AssetTypeActions/AssetTypeActions_LensFile.h"
#include "IAssetTypeActions.h"
#include "LensFile.h"
#include "LevelEditor.h"
#include "UI/LensDistortionEditorStyle.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "LensDistortionEditor"

DEFINE_LOG_CATEGORY(LogLensDistortionEditor);


void FLensDistortionEditorModule::StartupModule()
{
	FLensDistortionEditorStyle::Register();

	// Register AssetTypeActions
	auto RegisterAssetTypeAction = [this](const TSharedRef<IAssetTypeActions>& InAssetTypeAction)
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		RegisteredAssetTypeActions.Add(InAssetTypeAction);
		AssetTools.RegisterAssetTypeActions(InAssetTypeAction);
	};

	// register asset type actions
	RegisterAssetTypeAction(MakeShared<FAssetTypeActions_LensFile>());

	// register detail panel customization
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	{
		const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
		TSharedRef<FWorkspaceItem> BrowserGroup = MenuStructure.GetDeveloperToolsMiscCategory()->GetParent()->AddGroup(
			LOCTEXT("WorkspaceMenu_VirtualProduction", "Virtual Production"),
			FSlateIcon(),
			true);
	}
}

void FLensDistortionEditorModule::ShutdownModule()
{
	if (!IsEngineExitRequested() && GEditor && UObjectInitialized())
	{
		FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor");

		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(ULensFile::StaticClass()->GetFName());

		// Unregister AssetTypeActions
		FAssetToolsModule* AssetToolsModule = FModuleManager::GetModulePtr<FAssetToolsModule>("AssetTools");

		if (AssetToolsModule != nullptr)
		{
			IAssetTools& AssetTools = AssetToolsModule->Get();

			for (TSharedRef<IAssetTypeActions> Action : RegisteredAssetTypeActions)
			{
				AssetTools.UnregisterAssetTypeActions(Action);
			}
		}

		FLensDistortionEditorStyle::Unregister();
	}
}


IMPLEMENT_MODULE(FLensDistortionEditorModule, LensDistortionEditor);


#undef LOCTEXT_NAMESPACE
