// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsEditorFunctionLibrary.h"

#include "LevelSnapshotsLog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AssetViewUtils.h"
#include "IAssetTools.h"
#include "Editor.h"
#include "Engine/World.h"
#include "LevelSnapshot.h"
#include "Logging/MessageLog.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotEditorLibrary"

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
	const EObjectFlags AssetFlags = RF_Public | RF_Standalone;
	ULevelSnapshot* SnapshotAsset = NewObject<ULevelSnapshot>(SavePackage, *AssetName, AssetFlags, nullptr);

	if (SnapshotAsset)
	{
		SnapshotAsset->SetSnapshotName(*FileName);
		SnapshotAsset->SetSnapshotDescription(Description);

		SnapshotAsset->SnapshotWorld(TargetWorld);
		SnapshotAsset->MarkPackageDirty();

		FAssetRegistryModule::AssetCreated(SnapshotAsset);
		GenerateThumbnailForSnapshotAsset(SnapshotAsset);
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

void ULevelSnapshotsEditorFunctionLibrary::GenerateThumbnailForSnapshotAsset(ULevelSnapshot* Snapshot)
{
	if (!ensure(Snapshot))
	{
		return;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	TArray<FAssetData> SnapshotAssetData;
	AssetRegistry.GetAssetsByPackageName(FName(*Snapshot->GetPackage()->GetPathName()), SnapshotAssetData);
	
	if (ensureMsgf(SnapshotAssetData.Num(), TEXT("Failed to find asset data for asset we just saved. Investigate!")))
	{
		// Copied from FAssetFileContextMenu::ExecuteCaptureThumbnail
		FViewport* Viewport = GEditor->GetActiveViewport();

		if ( ensure(GCurrentLevelEditingViewportClient) && ensure(Viewport) )
		{
			//have to re-render the requested viewport
			FLevelEditorViewportClient* OldViewportClient = GCurrentLevelEditingViewportClient;
			//remove selection box around client during render
			GCurrentLevelEditingViewportClient = NULL;
			Viewport->Draw();

			AssetViewUtils::CaptureThumbnailFromViewport(Viewport, SnapshotAssetData);

			//redraw viewport to have the yellow highlight again
			GCurrentLevelEditingViewportClient = OldViewportClient;
			Viewport->Draw();
		}
	}
}

#undef LOCTEXT_NAMESPACE
