// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepEditorModule.h"

#include "AssetTypeActions_DataPrep.h"
#include "AssetTypeActions_DataPrepAsset.h"
#include "DataPrepEditor.h"
#include "DataprepEditorStyle.h"

#include "Developer/AssetTools/Public/IAssetTools.h"
#include "Developer/AssetTools/Public/AssetToolsModule.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "UObject/StrongObjectPtr.h"

const FName DataprepEditorAppIdentifier = FName(TEXT("DataprepEditorApp"));

#define LOCTEXT_NAMESPACE "DataprepEditorModule"

EAssetTypeCategories::Type IDataprepEditorModule::DataprepCategoryBit;

class FDataprepEditorModule : public IDataprepEditorModule
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override
	{
		FDataprepEditorStyle::Initialize();

		MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
		ToolBarExtensibilityManager = MakeShareable(new FExtensibilityManager);

		// Register asset type actions for DataPrepRecipe class
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		// Register Datasmith asset category to group asset type actions related to Datasmith together
		DataprepCategoryBit = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("Dataprep")), LOCTEXT("DataprepAssetCategory", "Dataprep"));

		TSharedPtr<FAssetTypeActions_Dataprep> DataprepRecipeTypeAction = MakeShareable(new FAssetTypeActions_Dataprep);
		AssetTools.RegisterAssetTypeActions(DataprepRecipeTypeAction.ToSharedRef());
		AssetTypeActionsArray.Add(DataprepRecipeTypeAction);

		TSharedPtr<FAssetTypeActions_DataprepAsset> DataprepAssetTypeAction = MakeShareable(new FAssetTypeActions_DataprepAsset);
		AssetTools.RegisterAssetTypeActions(DataprepAssetTypeAction.ToSharedRef());
		AssetTypeActionsArray.Add(DataprepAssetTypeAction);

		// Register mount point for Dataprep editors root package folder
		FPackageName::RegisterMountPoint( FDataprepEditor::GetRootPackagePath() + TEXT("/"), FDataprepEditor::GetRootTemporaryDir() );
	}

	virtual void ShutdownModule() override
	{
		MenuExtensibilityManager.Reset();
		ToolBarExtensibilityManager.Reset();

		// Unregister asset type actions
		if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools")))
		{
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			for (TSharedPtr<FAssetTypeActions_Base>& AssetTypeActions : AssetTypeActionsArray)
			{
				AssetTools.UnregisterAssetTypeActions(AssetTypeActions.ToSharedRef());
			}
		}
		AssetTypeActionsArray.Empty();

		FDataprepEditorStyle::Shutdown();

		// Unregister mount point for Dataprep editors root package folder
		FPackageName::UnRegisterMountPoint( FDataprepEditor::GetRootPackagePath() + TEXT("/"), FDataprepEditor::GetRootTemporaryDir() );
	}

	/** Gets the extensibility managers for outside entities to extend datasmith data prep editor's menus and toolbars */
	virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager() override { return MenuExtensibilityManager; }
	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager() override { return ToolBarExtensibilityManager; }

private:
	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;
	TArray<TSharedPtr<FAssetTypeActions_Base>> AssetTypeActionsArray;
};

IMPLEMENT_MODULE(FDataprepEditorModule, DataprepEditor)

#undef LOCTEXT_NAMESPACE
