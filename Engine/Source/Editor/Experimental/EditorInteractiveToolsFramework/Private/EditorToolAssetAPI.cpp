// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorToolAssetAPI.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"


#include "AssetRegistryModule.h"
#include "FileHelpers.h"
#include "PackageTools.h"
#include "ObjectTools.h"

// for content-browser things
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"


FString FEditorToolAssetAPI::GetActiveAssetFolderPath()
{
	check(false);  	// [RMS] this only works if we have an asset selected!

	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

	//ContentBrowser.CreatePathPicker()

	TArray<FAssetData> SelectedAssets;
	ContentBrowser.GetSelectedAssets(SelectedAssets);
	return SelectedAssets[0].PackagePath.ToString();
}



FString FEditorToolAssetAPI::MakePackageName(const FString& AssetName, const FString& FolderPath)
{
	return FolderPath + TEXT("/") + AssetName;
}


FString FEditorToolAssetAPI::MakeUniqueAssetName(const FString& AssetName, const FString& FolderPath)
{
	FString NewPackageName = MakePackageName(AssetName, FolderPath);
	if (FPackageName::DoesPackageExist(NewPackageName) == false)
	{
		return AssetName;
	}

	int Counter = 1;
	FString UniqueName = AssetName;
	while ( FPackageName::DoesPackageExist(NewPackageName) )
	{
		UniqueName = AssetName + FString::Printf(TEXT("_%d"), Counter++);
		NewPackageName = MakePackageName(UniqueName, FolderPath);
	}
	return UniqueName;
}


UPackage* FEditorToolAssetAPI::CreateNewPackage(const FString& AssetName, const FString& FolderPath)
{
	FString NewPackageName = MakePackageName(AssetName, FolderPath);
	NewPackageName = UPackageTools::SanitizePackageName(NewPackageName);
	UPackage* AssetPackage = CreatePackage(nullptr, *NewPackageName);
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