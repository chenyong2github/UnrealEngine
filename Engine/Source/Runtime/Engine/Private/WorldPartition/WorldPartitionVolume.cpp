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
void AWorldPartitionVolume::LoadIntersectingCells(bool bIsFromUserChange)
{
	if (UWorldPartition* WorldPartition = GetWorld()->GetWorldPartition())
	{
		WorldPartition->LoadEditorCells(GetIntersectingBounds(), bIsFromUserChange);
	}
}

void AWorldPartitionVolume::UnloadIntersectingCells(bool bIsFromUserChange)
{
	if (UWorldPartition* WorldPartition = GetWorld()->GetWorldPartition())
	{
		WorldPartition->UnloadEditorCells(GetIntersectingBounds(), bIsFromUserChange);
	}
}

FBox AWorldPartitionVolume::GetIntersectingBounds() const
{
	FVector Origin;
	FVector Extent;
	GetActorBounds(false, Origin, Extent);
	return FBox(Origin - Extent, Origin + Extent);
}
#endif

#undef LOCTEXT_NAMESPACE