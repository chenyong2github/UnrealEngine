// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsEditorFunctionLibrary.h"

#include "LevelSnapshotsLog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Editor.h"
#include "Engine/World.h"
#include "LevelSnapshot.h"
#include "LevelSnapshotsFunctionLibrary.h"
#include "Logging/MessageLog.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotEditorLibrary"

void ULevelSnapshotsEditorFunctionLibrary::SaveLevelSnapshotToDisk(ULevelSnapshot* LevelSnapshot, const FString FileName, const FString FolderPath)
{
	if (!LevelSnapshot)
	{
		FMessageLog("Blueprint").Warning(LOCTEXT("SaveLevelSnapshotToDisk_InvalidSnapshot", "SaveLevelSnapshotToDisk: LevelSnapshot must be non-null."));
		return;
	}

	IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	FString AssetName;
	FString PackageName;

	FString BasePackageName = FPaths::Combine(FolderPath, FileName);

	BasePackageName.RemoveFromStart(TEXT("/"));
	BasePackageName.RemoveFromStart(TEXT("Content/"));
	BasePackageName.StartsWith(TEXT("Game/")) == true ? BasePackageName.InsertAt(0, TEXT("/")) : BasePackageName.InsertAt(0, TEXT("/Game/"));

	AssetTools.CreateUniqueAssetName(BasePackageName, TEXT(""), PackageName, AssetName);

	UPackage* SavePackage = CreatePackage(*PackageName);
	EObjectFlags AssetFlags = RF_Public | RF_Standalone;
	ULevelSnapshot* SnapshotAsset = NewObject<ULevelSnapshot>(SavePackage, *AssetName, AssetFlags, LevelSnapshot);

	if (SnapshotAsset)
	{
		SnapshotAsset->MarkPackageDirty();

		FAssetRegistryModule::AssetCreated(SnapshotAsset);
	}

};

ULevelSnapshot* ULevelSnapshotsEditorFunctionLibrary::TakeLevelSnapshotAndSaveToDisk(const UObject* WorldContextObject, const FString FileName, const FString FolderPath, const FString Description)
{
	UWorld* TargetWorld = nullptr;
	if (WorldContextObject)
	{
		TargetWorld = WorldContextObject->GetWorld();
	}

	if (!TargetWorld)
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Snapshot taken with no valid World set"));
		return nullptr;
	}

	IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	FString AssetName;
	FString PackageName;

	FString BasePackageName = FPaths::Combine(FolderPath, FileName);

	BasePackageName.RemoveFromStart(TEXT("/"));
	BasePackageName.RemoveFromStart(TEXT("Content/"));
	BasePackageName.StartsWith(TEXT("Game/")) == true ? BasePackageName.InsertAt(0, TEXT("/")) : BasePackageName.InsertAt(0, TEXT("/Game/"));

	AssetTools.CreateUniqueAssetName(BasePackageName, TEXT(""), PackageName, AssetName);

	UPackage* SavePackage = CreatePackage(*PackageName);
	EObjectFlags AssetFlags = RF_Public | RF_Standalone;
	ULevelSnapshot* SnapshotAsset = NewObject<ULevelSnapshot>(SavePackage, *AssetName, AssetFlags, nullptr);

	if (SnapshotAsset)
	{
		SnapshotAsset->SetSnapshotName(*FileName);
		SnapshotAsset->SetSnapshotDescription(Description);

		SnapshotAsset->SnapshotWorld(TargetWorld);

		SnapshotAsset->MarkPackageDirty();

		FAssetRegistryModule::AssetCreated(SnapshotAsset);
	}

	return SnapshotAsset;
}

void ULevelSnapshotsEditorFunctionLibrary::TakeAndSaveLevelSnapshotEditorWorld(const FString FileName, const FString FolderPath, const FString Description)
{
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext(false).World() : nullptr;
	if (EditorWorld)
	{
		TakeLevelSnapshotAndSaveToDisk(EditorWorld, FileName, FolderPath, Description);
	}
	else
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Could not find valid Editor World."));
	}
}

#undef LOCTEXT_NAMESPACE