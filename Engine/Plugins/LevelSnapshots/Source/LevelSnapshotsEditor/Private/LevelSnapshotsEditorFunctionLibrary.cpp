// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsEditorFunctionLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Editor.h"
#include "Engine/World.h"
#include "LevelSnapshot.h"
#include "Logging/MessageLog.h"

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

	// TODO. that is just for testing now
	ULevelSnapshot* SnapshotAsset = NewObject<ULevelSnapshot>(CreatePackage(*PackageName), *AssetName, RF_Public, LevelSnapshot);
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World != nullptr)
	{
		SnapshotAsset->MapName = World->GetMapName();

		SnapshotAsset->SnapshotWorld(World);
	}


	if (SnapshotAsset)
	{
		SnapshotAsset->MarkPackageDirty();

		FAssetRegistryModule::AssetCreated(SnapshotAsset);
	}

};

#undef LOCTEXT_NAMESPACE