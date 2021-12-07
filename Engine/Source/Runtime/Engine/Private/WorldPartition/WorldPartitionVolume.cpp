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
#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif
}

#if WITH_EDITOR
void AWorldPartitionVolume::LoadIntersectingCells(bool bIsFromUserChange)
{
	if (UWorldPartition* WorldPartition = GetWorld()->GetWorldPartition())
	{
		WorldPartition->LoadEditorCells(GetStreamingBounds(), bIsFromUserChange);
	}
}

void AWorldPartitionVolume::UnloadIntersectingCells(bool bIsFromUserChange)
{
	if (UWorldPartition* WorldPartition = GetWorld()->GetWorldPartition())
	{
		WorldPartition->UnloadEditorCells(GetStreamingBounds(), bIsFromUserChange);
	}
}
#endif

#undef LOCTEXT_NAMESPACE