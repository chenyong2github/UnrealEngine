// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/EditorToolAssetAPI.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/Classes/Engine/World.h"


#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "FileHelpers.h"

// for content-browser things
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"


FString FEditorToolAssetAPI::GetActiveAssetFolderPath()
{
	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
	return ContentBrowser.GetCurrentPath();
}

FString FEditorToolAssetAPI::GetWorldRelativeAssetRootPath(const UWorld* World)
{
	if (ensure(World->GetOutermost() != nullptr) == false)
	{
		return TEXT("/Game/");
	}
	FString WorldPackageName = World->GetOutermost()->GetName();
	FString WorldPackageFolder = FPackageName::GetLongPackagePath(WorldPackageName);
	return WorldPackageFolder;
}


FString FEditorToolAssetAPI::InteractiveSelectAssetPath(const FString& DefaultAssetName, const FText& DialogTitleMessage)
{
	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

	FString UseDefaultAssetName = DefaultAssetName;
	FString CurrentPath = GetActiveAssetFolderPath();
	if (CurrentPath.IsEmpty() == false)
	{
		UseDefaultAssetName = MakeUniqueAssetName(CurrentPath, DefaultAssetName);
	}

	FSaveAssetDialogConfig Config;
	Config.DefaultAssetName = UseDefaultAssetName;
	Config.DialogTitleOverride = DialogTitleMessage;
	Config.DefaultPath = CurrentPath;
	return ContentBrowser.CreateModalSaveAssetDialog(Config);
}


UPackage* FEditorToolAssetAPI::MakeNewAssetPackage(const FString& FolderPath, const FString& AssetBaseName, FString& UniqueAssetName)
{
	FString UniquePackageName;
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(
		FolderPath + TEXT("/") + AssetBaseName, TEXT(""), UniquePackageName, UniqueAssetName);

	UPackage* AssetPackage = CreatePackage( *UniquePackageName);
	return AssetPackage;
}


FString FEditorToolAssetAPI::MakeUniqueAssetName(const FString& FolderPath, const FString& AssetBaseName)
{
	FString UniquePackageName, UniqueAssetName;
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	AssetToolsModule.Get().CreateUniqueAssetName(
		FolderPath + TEXT("/") + AssetBaseName, TEXT(""), UniquePackageName, UniqueAssetName);
	return UniqueAssetName;
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


void FEditorToolAssetAPI::NotifyGeneratedAssetModified(UObject* Asset, UPackage* AssetPackage)
{
	Asset->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Asset);
}