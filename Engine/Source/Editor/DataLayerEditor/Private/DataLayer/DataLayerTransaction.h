// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ScopedTransaction.h"
#include "WorldPartition/WorldPartition.h"
#include "Engine/World.h"
#include "Editor.h"

class FScopedDataLayerTransaction
{
public:
	FScopedDataLayerTransaction(const FText& SessionName, UWorld* InWorld)
	: WorldPtr(InWorld)
	, bUndoTransaction(false)
	{
		check(InWorld);
		ScopedTransaction = new FScopedTransaction(SessionName);
		UWorldPartition* WorldPartition = InWorld->GetWorldPartition();
		WorldPartition->OnCancelWorldPartitionUpdateEditorCells.AddRaw(this, &FScopedDataLayerTransaction::OnCancelUpdateEditorCells);
	}

	~FScopedDataLayerTransaction()
	{
		UWorld* World = WorldPtr.Get();
		if (UWorldPartition* WorldPartition = World ? World->GetWorldPartition() : nullptr)
		{
			WorldPartition->OnCancelWorldPartitionUpdateEditorCells.RemoveAll(this);
		}
		delete ScopedTransaction;
		if (bUndoTransaction)
		{
			GEditor->UndoTransaction();
		}
	}

private:

	void OnCancelUpdateEditorCells(UWorldPartition* InWorldPartition)
	{
		UWorld* World = WorldPtr.Get();
		if (World && (World->GetWorldPartition() == InWorldPartition))
		{
			bUndoTransaction = true;
		}
	}

	FScopedTransaction* ScopedTransaction;
	TWeakObjectPtr<UWorld> WorldPtr;
	bool bUndoTransaction;
};