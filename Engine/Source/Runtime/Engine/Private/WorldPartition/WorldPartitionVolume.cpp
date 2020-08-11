// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionVolume.h"
#include "WorldPartition/WorldPartitionEditorHash.h"
#include "WorldPartition/WorldPartition.h"
#include "Engine/World.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "WorldPartitionEditor"

AWorldPartitionVolume::AWorldPartitionVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
void AWorldPartitionVolume::LoadIntersectingCells()
{
	if (UWorldPartition* WorldPartition = GetWorld()->GetWorldPartition())
	{
		FVector Origin;
		FVector Extent;
		this->GetActorBounds(false, Origin, Extent);

		TArray<UWorldPartitionEditorCell*> IntersectingCells;
		if (WorldPartition->EditorHash->GetIntersectingCells(FBox(Origin - Extent, Origin + Extent), IntersectingCells))
		{
			FScopedSlowTask SlowTask(IntersectingCells.Num(), LOCTEXT("LoadingCells", "Loading cells..."));
			SlowTask.MakeDialog();

			WorldPartition->LoadCells(IntersectingCells);
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE