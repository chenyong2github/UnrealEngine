// Copyright Epic Games, Inc. All Rights Reserved.


#include "CameraCalibrationEditorModule.h"

#include "AssetEditor/CameraCalibrationCommands.h"
#include "AssetToolsModule.h"
#include "AssetTypeActions/AssetTypeActions_LensFile.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "LensFile.h"
#include "LevelEditor.h"
#include "UI/CameraCalibrationEditorStyle.h"
#include "UI/CameraCalibrationMenuEntry.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "CameraCalibrationEditor"

DEFINE_LOG_CATEGORY(LogCameraCalibrationEditor);


void FCameraCalibrationEditorModule::StartupModule()
{
	FCameraCalibrationCommands::Register();
	FCameraCalibrationEditorStyle::Register();

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

	FCameraCalibrationMenuEntry::Register();
}

void FCameraCalibrationEditorModule::ShutdownModule()
{
	if (!IsEngineExitRequested() && GEditor && UObjectInitialized())
	{
		FCameraCalibrationMenuEntry::Unregister();

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

		FCameraCalibrationEditorStyle::Unregister();
		FCameraCalibrationCommands::Unregister();
	}
}


IMPLEMENT_MODULE(FCameraCalibrationEditorModule, CameraCalibrationEditor);


#undef LOCTEXT_NAMESPACE
