// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetTypeActions/AssetTypeActions_LevelSnapshot.h"

#include "LevelSnapshot.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

FText FAssetTypeActions_LevelSnapshot::GetName() const
{
	return LOCTEXT("AssetTypeActions_LevelSnapshot_Name", "Level Snapshot");
}

UClass* FAssetTypeActions_LevelSnapshot::GetSupportedClass() const
{
	return ULevelSnapshot::StaticClass();
}

void FAssetTypeActions_LevelSnapshot::GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder)
{
	FAssetTypeActions_Base::GetActions(InObjects, MenuBuilder);

	TArray<TWeakObjectPtr<ULevelSnapshot>> LevelSnapshotAssets = GetTypedWeakObjectPtrs<ULevelSnapshot>(InObjects);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("AssetTypeActions_LevelSnapshot_TakeSnapshot", "Take Snapshot"),
		LOCTEXT("AssetTypeActions_LevelSnapshot_TakeSnapshotToolTip", "Record a snapshot of the current map to this snapshot asset. Select only one Level Snapshot asset at a time."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([=] {
				for (const TWeakObjectPtr<ULevelSnapshot>& LevelSnapshotAsset : LevelSnapshotAssets)
				{
					if (LevelSnapshotAsset.IsValid())
					{
						if (UWorld* World = GEditor->GetEditorWorldContext().World())
						{
							LevelSnapshotAsset->MapName = World->GetMapName();

							LevelSnapshotAsset->SnapshotWorld(World);

							LevelSnapshotAsset->MarkPackageDirty();
						}
					}
				}
				}),
			FCanExecuteAction::CreateLambda([=] {
					// We only want to save a snapshot to a single asset at a time, so let's ensure
					// the number of selected assets is exactly one.
					return LevelSnapshotAssets.Num() == 1;
				})
			)
	);
}

#undef LOCTEXT_NAMESPACE