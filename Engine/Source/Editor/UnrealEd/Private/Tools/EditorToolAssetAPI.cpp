// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Tools/EditorToolAssetAPI.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"


#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "FileHelpers.h"

// for content-browser things
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"


FString FEditorToolAssetAPI::GetActiveAssetFolderPath()
{
	check(false);  	// this only works if we have an asset selected!

	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

	//ContentBrowser.CreatePathPicker()

	TArray<FAssetData> SelectedAssets;
	ContentBrowser.GetSelectedAssets(SelectedAssets);
	return SelectedAssets[0].PackagePath.ToString();
}




UPackage* FEditorToolAssetAPI::MakeNewAssetPackage(const FString& FolderPath, const FString& AssetBaseName, FString& UniqueAssetName)
{
	FString UniquePackageName;
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(
		FolderPath + TEXT("/") + AssetBaseName, TEXT(""), UniquePackageName, UniqueAssetName);

	UPackage* AssetPackage = CreatePackage(nullptr, *UniquePackageName);
	return AssetPackage;

}



void FEditorToolAssetAPI::InteractiveSaveGeneratedAsset(UObject* Asset, UPackage* AssetPackage)
{
	Asset->MarkPackageDirty();

	FAssetRegistryModule::AssetCreated(Asset);

	TArray<UPackage*> PackagesToSave;
	PackagesToSave.Add(AssetPackage);
	bool bCheckDirty = true;
	bool bPromptToSave = true;
	FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptToSave);
}


void FEditorToolAssetAPI::AutoSaveGeneratedAsset(UObject* Asset, UPackage* AssetPackage)
{
	Asset->MarkPackageDirty();

	FAssetRegistryModule::AssetCreated(Asset);

	TArray<UPackage*> PackagesToSave;
	PackagesToSave.Add(AssetPackage);
	bool bCheckDirty = true;
	bool bPromptToSave = false;
	FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, bCheckDirty, bPromptToSave);
}