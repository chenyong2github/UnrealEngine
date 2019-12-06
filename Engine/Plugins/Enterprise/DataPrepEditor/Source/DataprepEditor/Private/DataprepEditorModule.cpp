// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepEditorModule.h"

#include "AssetTypeActions_Dataprep.h"
#include "AssetTypeActions_DataprepAsset.h"
#include "AssetTypeActions_DataprepAssetInterface.h"
#include "DataprepAssetProducers.h"
#include "DataprepEditor.h"
#include "DataprepEditorStyle.h"
#include "Widgets/DataprepWidgets.h"
#include "Widgets/SDataprepEditorViewport.h"
#include "Widgets/SDataprepProducersWidget.h"

#include "Developer/AssetTools/Public/IAssetTools.h"
#include "Developer/AssetTools/Public/AssetToolsModule.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "UObject/StrongObjectPtr.h"

#include "Widgets/SNullWidget.h"

const FName DataprepEditorAppIdentifier = FName(TEXT("DataprepEditorApp"));

EAssetTypeCategories::Type IDataprepEditorModule::DataprepCategoryBit;

#define LOCTEXT_NAMESPACE "DataprepEditorModule"

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

		// Register Dataprep category to group together asset type actions related to Dataprep
		DataprepCategoryBit = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("Dataprep")), LOCTEXT("DataprepAssetCategory", "Dataprep"));

		TSharedPtr<FAssetTypeActions_DataprepAssetInterface> DataprepAssetInterfaceTypeAction = MakeShared<FAssetTypeActions_DataprepAssetInterface>();
		AssetTools.RegisterAssetTypeActions(DataprepAssetInterfaceTypeAction.ToSharedRef());
		AssetTypeActionsArray.Add(DataprepAssetInterfaceTypeAction);

		TSharedPtr<FAssetTypeActions_Dataprep> DataprepRecipeTypeAction = MakeShareable(new FAssetTypeActions_Dataprep);
		AssetTools.RegisterAssetTypeActions(DataprepRecipeTypeAction.ToSharedRef());
		AssetTypeActionsArray.Add(DataprepRecipeTypeAction);

		TSharedPtr<FAssetTypeActions_DataprepAsset> DataprepAssetTypeAction = MakeShareable(new FAssetTypeActions_DataprepAsset);
		AssetTools.RegisterAssetTypeActions(DataprepAssetTypeAction.ToSharedRef());
		AssetTypeActionsArray.Add(DataprepAssetTypeAction);

		TSharedPtr<FAssetTypeActions_DataprepAssetInstance> DataprepAssetInstanceTypeAction = MakeShareable(new FAssetTypeActions_DataprepAssetInstance);
		AssetTools.RegisterAssetTypeActions(DataprepAssetInstanceTypeAction.ToSharedRef());
		AssetTypeActionsArray.Add(DataprepAssetInstanceTypeAction);

		// Register mount point for Dataprep editors root package folder
		FPackageName::RegisterMountPoint( FDataprepEditor::GetRootPackagePath() + TEXT("/"), FDataprepEditor::GetRootTemporaryDir() );

		// Register the details customizer
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked< FPropertyEditorModule >( TEXT("PropertyEditor") );
		PropertyModule.RegisterCustomClassLayout( UDataprepAssetProducers::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic( &FDataprepAssetProducersDetails::MakeDetails ) );

		SDataprepEditorViewport::LoadDefaultSettings();
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

		// Register the details customizer
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked< FPropertyEditorModule >( TEXT("PropertyEditor") );
		PropertyModule.UnregisterCustomClassLayout( TEXT("DataprepAssetProducers") );
	}

	/** Gets the extensibility managers for outside entities to extend datasmith data prep editor's menus and toolbars */
	virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager() override { return MenuExtensibilityManager; }
	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager() override { return ToolBarExtensibilityManager; }

	virtual TSharedRef<SWidget> CreateDataprepProducersWidget(UDataprepAssetProducers* AssetProducers) override
	{
		TSharedPtr<FUICommandList> CommandList = MakeShareable( new FUICommandList );
		return AssetProducers ? SNew(SDataprepProducersWidget, AssetProducers, CommandList) : SNullWidget::NullWidget;
	}

	virtual TSharedRef<SWidget> CreateDataprepDetailsView(UObject* ObjectToDetail) override
	{
		if(ObjectToDetail)
		{
			return SNew(SDataprepDetailsView).Object( ObjectToDetail );
		}

		return SNullWidget::NullWidget;
	}


private:
	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;
	TArray<TSharedPtr<FAssetTypeActions_Base>> AssetTypeActionsArray;
};

IMPLEMENT_MODULE(FDataprepEditorModule, DataprepEditor)

#undef LOCTEXT_NAMESPACE
