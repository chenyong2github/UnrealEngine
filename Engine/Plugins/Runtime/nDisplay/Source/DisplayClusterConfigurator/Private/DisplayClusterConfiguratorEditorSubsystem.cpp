// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorEditorSubsystem.h"

#include "DisplayClusterConfiguratorEditorData.h"
#include "DisplayClusterConfigurationStrings.h"
#include "IDisplayClusterConfiguration.h"

#include "AssetToolsModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/WeakObjectPtr.h"

bool UDisplayClusterConfiguratorEditorSubsystem::ReimportAsset(UDisplayClusterConfiguratorEditorData* InConfiguratorEditorData)
{
	if (InConfiguratorEditorData != nullptr)
	{
		FString NewFileName = FPaths::GetBaseFilename(InConfiguratorEditorData->PathToConfig);
		const FString PackagePath = FPackageName::GetLongPackagePath(InConfiguratorEditorData->GetOutermost()->GetName());
		RenameAssets(InConfiguratorEditorData, PackagePath, NewFileName);

		return ReloadConfig(InConfiguratorEditorData, InConfiguratorEditorData->PathToConfig);
	}
	
	return false;
}

bool UDisplayClusterConfiguratorEditorSubsystem::ReloadConfig(UDisplayClusterConfiguratorEditorData* InConfiguratorEditorData, const FString& InConfigPath)
{
	if (InConfiguratorEditorData != nullptr)
	{
		UDisplayClusterConfigurationData* NewConfig = IDisplayClusterConfiguration::Get().LoadConfig(InConfigPath);
		if (NewConfig)
		{
			InConfiguratorEditorData->PathToConfig = InConfigPath;
			InConfiguratorEditorData->nDisplayConfig = NewConfig;

			return true;
		}
	}

	return false;
}

bool UDisplayClusterConfiguratorEditorSubsystem::SaveConfig(UDisplayClusterConfiguratorEditorData* InConfiguratorEditorData, const FString& InConfigPath)
{
	if (InConfiguratorEditorData)
	{
		FString ConfigFileExtension = FPaths::GetExtension(InConfigPath, true);
		FString FilePathToSave = InConfigPath;
		bool bIsCFGFile = false;

		FString JsonExtension = FString(".") + FString(DisplayClusterConfigurationStrings::file::FileExtJson);
		FString CfgExtension = FString(".") + FString(DisplayClusterConfigurationStrings::file::FileExtCfg);

		// Update file extension to .ndisplay if original file is .cfg
		if (ConfigFileExtension.Equals(CfgExtension))
		{
			FilePathToSave = FPaths::ChangeExtension(InConfigPath, JsonExtension);
			bIsCFGFile = true;
		}

		if (InConfiguratorEditorData->nDisplayConfig != nullptr)
		{
			if (!InConfiguratorEditorData->PathToConfig.Equals(FilePathToSave))
			{
				FString NewFileName = FPaths::GetBaseFilename(FilePathToSave);
				const FString PackagePath = FPackageName::GetLongPackagePath(InConfiguratorEditorData->GetOutermost()->GetName());
				RenameAssets(InConfiguratorEditorData, PackagePath, NewFileName);
			}

			InConfiguratorEditorData->PathToConfig = FilePathToSave;
			return IDisplayClusterConfiguration::Get().SaveConfig(InConfiguratorEditorData->nDisplayConfig, FilePathToSave);
		}
	}

	return false;
}

bool UDisplayClusterConfiguratorEditorSubsystem::RenameAssets(const TWeakObjectPtr<UObject>& InAsset, const FString& InNewPackagePath, const FString& InNewName)
{
	FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));

	TArray<FAssetRenameData> RenameData;

	RenameData.Add(FAssetRenameData(InAsset, InNewPackagePath, InNewName));

	AssetToolsModule.Get().RenameAssetsWithDialog(RenameData);

	return true;
}


