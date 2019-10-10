// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataPrepCoreModule.h"

#include "AssetTypeActions_DataprepAssetInterface.h"
#include "DataprepCorePrivateUtils.h"

#include "Developer/AssetTools/Public/IAssetTools.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "DataprepCoreModule"

EAssetTypeCategories::Type IDataprepCoreModule::DataprepCategoryBit;

class FDataprepCoreModule : public IDataprepCoreModule
{
public:
	virtual void StartupModule() override
	{
		// Register asset type actions for DataPrepRecipe class
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		// Register Dataprep category to group together asset type actions related to Dataprep
		DataprepCategoryBit = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("Dataprep")), LOCTEXT("DataprepCoreModule", "Dataprep"));

		DataprepAssetInterfaceTypeAction = MakeShareable(new FAssetTypeActions_DataprepAssetInterface);
		AssetTools.RegisterAssetTypeActions(DataprepAssetInterfaceTypeAction.ToSharedRef());

		// Register mount point for Dataprep core library root package folder
		FPackageName::RegisterMountPoint( DataprepCorePrivateUtils::GetRootPackagePath() + TEXT("/"), DataprepCorePrivateUtils::GetRootTemporaryDir()	);
	}

	virtual void ShutdownModule() override
	{
		// Unregister asset type actions
		if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools")))
		{
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			AssetTools.UnregisterAssetTypeActions(DataprepAssetInterfaceTypeAction.ToSharedRef());
		}
		DataprepAssetInterfaceTypeAction.Reset();

		// Unregister mount point for Dataprep core library root package folder
		FPackageName::UnRegisterMountPoint( DataprepCorePrivateUtils::GetRootPackagePath() + TEXT("/"), DataprepCorePrivateUtils::GetRootTemporaryDir() );
	}

private:
	TSharedPtr<FAssetTypeActions_DataprepAssetInterface> DataprepAssetInterfaceTypeAction;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE( FDataprepCoreModule, DataprepCore )
