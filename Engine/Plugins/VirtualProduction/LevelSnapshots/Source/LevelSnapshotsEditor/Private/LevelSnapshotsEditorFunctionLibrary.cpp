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
#include "LevelSnapshotsEditorStyle.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameplayMediaEncoder/Private/GameplayMediaEncoderCommon.h"
#include "Logging/MessageLog.h"
#include "Misc/MessageDialog.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotEditorLibrary"

ULevelSnapshot* ULevelSnapshotsEditorFunctionLibrary::TakeLevelSnapshotAndSaveToDisk(
	const UObject* WorldContextObject, const FString& FileName, const FString& FolderPath, const FString& Description, const bool bShouldCreateUniqueFileName)
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

	FString AssetName = FileName;
	
	FString BasePackageName = FPaths::Combine(FolderPath, FileName);

	BasePackageName.RemoveFromStart(TEXT("/"));
	BasePackageName.RemoveFromStart(TEXT("Content/"));
	BasePackageName.StartsWith(TEXT("Game/")) == true ? BasePackageName.InsertAt(0, TEXT("/")) : BasePackageName.InsertAt(0, TEXT("/Game/"));

	FString PackageName = BasePackageName;

	if (bShouldCreateUniqueFileName)
	{
		IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		
		AssetTools.CreateUniqueAssetName(BasePackageName, TEXT(""), PackageName, AssetName);
	}

	const FString& PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	const bool bPathIsValid = FPaths::ValidatePath(PackageFileName);

	bool bProceedWithSave = bPathIsValid;

	if (bProceedWithSave && !bShouldCreateUniqueFileName)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		FAssetData ExistingAsset = AssetRegistryModule.Get().GetAssetByObjectPath(FName(*(BasePackageName + "." + FileName)));
		if ( ExistingAsset.IsValid() && ExistingAsset.AssetClass == *ULevelSnapshot::StaticClass()->GetName() )
		{
			EAppReturnType::Type ShouldReplace = FMessageDialog::Open( EAppMsgType::YesNo, FText::Format(LOCTEXT("ReplaceAssetMessage", "{0} already exists. Do you want to replace it?"), FText::FromString(FileName)) );
			bProceedWithSave = (ShouldReplace == EAppReturnType::Yes);
		}
	}

	if (bProceedWithSave)
	{
		// Show notification
		FNotificationInfo Notification(NSLOCTEXT("LevelSnapshots", "NotificationFormatText_CreatingSnapshot", "Creating Level Snapshot"));
		Notification.Image = FLevelSnapshotsEditorStyle::GetBrush(TEXT("LevelSnapshots.ToolbarButton"));
		Notification.bUseThrobber = true;
		Notification.bUseSuccessFailIcons = true;
		Notification.ExpireDuration = 2.f;
		Notification.bFireAndForget = false;
		
		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Notification);
		NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
		ON_SCOPE_EXIT
		{
			NotificationItem->ExpireAndFadeout();
		};

		UPackage* SavePackage = CreatePackage(*PackageName);
		const EObjectFlags AssetFlags = RF_Public | RF_Standalone;
		ULevelSnapshot* SnapshotAsset = NewObject<ULevelSnapshot>(SavePackage, *AssetName, AssetFlags, nullptr);

		check(SavePackage && SnapshotAsset);
		
		SnapshotAsset->SetSnapshotName(*FileName);
		SnapshotAsset->SetSnapshotDescription(Description);

		SnapshotAsset->SnapshotWorld(TargetWorld);
		SnapshotAsset->MarkPackageDirty();

		FAssetRegistryModule::AssetCreated(SnapshotAsset);
		GenerateThumbnailForSnapshotAsset(SnapshotAsset);

		// Notify the user of the outcome
		if (bPathIsValid && UPackage::SavePackage(SavePackage, SnapshotAsset, RF_Public | RF_Standalone, *PackageFileName))
		{
			// If successful
			NotificationItem->SetText(
				FText::Format(
					NSLOCTEXT("LevelSnapshots", "NotificationFormatText_CreateSnapshotSuccess", "Successfully created Level Snapshot \"{0}\""), FText::FromString(FileName)));
			NotificationItem->SetCompletionState(SNotificationItem::CS_Success);
		}
		else
		{
			NotificationItem->SetText(
				FText::Format(
					NSLOCTEXT("LevelSnapshots", "NotificationFormatText_CreateSnapshotSuccess", "Failed to create Level Snapshot \"{0}\". Check the file name."), FText::FromString(FileName)));
			NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
		}

		return SnapshotAsset;
	}

	return nullptr;
}

void ULevelSnapshotsEditorFunctionLibrary::TakeAndSaveLevelSnapshotEditorWorld(const FString& FileName, const FString& FolderPath, const FString& Description)
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
