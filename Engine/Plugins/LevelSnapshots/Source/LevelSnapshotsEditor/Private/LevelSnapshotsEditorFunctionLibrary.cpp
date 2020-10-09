// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsEditorFunctionLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "LevelSnapshot.h"
#include "Logging/MessageLog.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotEditorLibrary"

void ULevelSnapshotsEditorFunctionLibrary::SaveLevelSnapshotToDisk(const UObject* WorldContextObject, ULevelSnapshot* LevelSnapshot, const FString FileName, const FString FolderPath)
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

	ULevelSnapshot* SnapshotAsset = NewObject<ULevelSnapshot>(CreatePackage(NULL, *PackageName), *AssetName, RF_Public, LevelSnapshot);

	UWorld* TargetWorld = nullptr;
	if (WorldContextObject)
	{
		TargetWorld = WorldContextObject->GetWorld();
	}
	SnapshotAsset->SnapshotWorld(TargetWorld);

	if (SnapshotAsset)
	{
		SnapshotAsset->MarkPackageDirty();

		FAssetRegistryModule::AssetCreated(SnapshotAsset);
	}

};

#undef LOCTEXT_NAMESPACE