// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartition/WorldPartitionRuntimeSpatialHashCell.h"

UWorldPartitionRuntimeSpatialHashCell::UWorldPartitionRuntimeSpatialHashCell(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
, Level(0)
{}

#if WITH_EDITOR
void UWorldPartitionRuntimeSpatialHashCell::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);

	if (ActorContainer)
	{
		for (auto& ActorPair : ActorContainer->Actors)
		{
			// Don't use AActor::Rename here since the actor is not par of the world, it's only a duplication template.
			ActorPair.Value->UObject::Rename(nullptr, ActorContainer);
		}
	}
}
#endif

