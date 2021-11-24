// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseWatchManagerDefaultHierarchy.h"
#include "PoseWatchManagerFolderTreeItem.h"
#include "PoseWatchManagerPoseWatchTreeItem.h"
#include "PoseWatchManagerDefaultMode.h"
#include "EditorFolderUtils.h"
#include "Engine/PoseWatch.h"
#include "Animation/AnimBlueprint.h"
#include "SPoseWatchManager.h"

FPoseWatchManagerDefaultHierarchy::FPoseWatchManagerDefaultHierarchy(FPoseWatchManagerDefaultMode* InMode) : Mode(InMode) {}

FPoseWatchManagerTreeItemPtr FPoseWatchManagerDefaultHierarchy::FindParent(const IPoseWatchManagerTreeItem& Item, const TMap<FObjectKey, FPoseWatchManagerTreeItemPtr>& Items) const
{
	UPoseWatchFolder* ParentFolder = nullptr;
	if (const FPoseWatchManagerFolderTreeItem* FolderTreeItem = Item.CastTo<FPoseWatchManagerFolderTreeItem>())
	{
		ParentFolder = FolderTreeItem->PoseWatchFolder->GetParent();

	}
	else if (const FPoseWatchManagerPoseWatchTreeItem* PoseWatchTreeItem = Item.CastTo<FPoseWatchManagerPoseWatchTreeItem>())
	{
		ParentFolder = PoseWatchTreeItem->PoseWatch->GetParent();
	}

	if (ParentFolder)
	{
		const FPoseWatchManagerTreeItemPtr* ParentTreeItem = Items.Find(ParentFolder);
		if (ParentTreeItem)
		{
			return *ParentTreeItem;
		}
	}
	
	return nullptr;
}

void FPoseWatchManagerDefaultHierarchy::CreateItems(TArray<FPoseWatchManagerTreeItemPtr>& OutItems) const
{
	UAnimBlueprint* AnimBlueprint = Mode->PoseWatchManager->AnimBlueprint;
	for (int32 Index = 0; Index <  AnimBlueprint->PoseWatchFolders.Num(); ++Index)
	{
		TObjectPtr<UPoseWatchFolder>& PoseWatchFolder = AnimBlueprint->PoseWatchFolders[Index];
		if (PoseWatchFolder)
		{
			FPoseWatchManagerTreeItemPtr PoseWatchFolderItem = Mode->PoseWatchManager->CreateItemFor<FPoseWatchManagerFolderTreeItem>(PoseWatchFolder);
			OutItems.Add(PoseWatchFolderItem);
		}
		else
		{
			AnimBlueprint->PoseWatchFolders.RemoveAtSwap(Index);
		}
	}
	for (int32 Index = 0; Index < AnimBlueprint->PoseWatches.Num(); ++Index)
	{
		TObjectPtr<UPoseWatch>& PoseWatch = AnimBlueprint->PoseWatches[Index];
		if (PoseWatch)
		{
			if (!PoseWatch->GetShouldDeleteOnDeselect())
			{
				FPoseWatchManagerTreeItemPtr PoseWatchItem = Mode->PoseWatchManager->CreateItemFor<FPoseWatchManagerPoseWatchTreeItem>(PoseWatch);
				OutItems.Add(PoseWatchItem);
			}
		}
		else
		{
			AnimBlueprint->PoseWatches.RemoveAtSwap(Index);
		}
	}
}
