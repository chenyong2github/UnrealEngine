// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/LoaderAdapter/LoaderAdapterSpatial.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionEditorHash.h"
#include "Engine/Level.h"
#include "Engine/World.h"

#if WITH_EDITOR
ILoaderAdapterSpatial::ILoaderAdapterSpatial(UWorld* InWorld)
	: ILoaderAdapter(InWorld)
	, bIncludeSpatiallyLoadedActors(true)
	, bIncludeNonSpatiallyLoadedActors(false)
{}

void ILoaderAdapterSpatial::ForEachActor(TFunctionRef<void(const FWorldPartitionHandle&)> InOperation) const
{
	if (UWorldPartition* WorldPartition = GetWorld()->GetWorldPartition())
	{
		HandleIntersectingContainer(WorldPartition, InOperation);
	}
}

void ILoaderAdapterSpatial::HandleIntersectingContainer(UWorldPartition* InWorldPartition, TFunctionRef<void(const FWorldPartitionHandle&)> InOperation) const
{
	const FTransform InstanceTransform = InWorldPartition->GetInstanceTransform();
	const FBox LocalBoundingBox = GetBoundingBox()->InverseTransformBy(InstanceTransform);

	UWorldPartitionEditorHash::FForEachIntersectingActorParams ForEachIntersectingActorParams = UWorldPartitionEditorHash::FForEachIntersectingActorParams()
		.SetIncludeSpatiallyLoadedActors(bIncludeSpatiallyLoadedActors)
		.SetIncludeNonSpatiallyLoadedActors(bIncludeNonSpatiallyLoadedActors);

	InWorldPartition->EditorHash->ForEachIntersectingActor(LocalBoundingBox, [this, InWorldPartition, &InstanceTransform, &InOperation](FWorldPartitionActorDesc* ActorDesc)
	{
		const FBox WorldActorEditorBox = ActorDesc->GetEditorBounds().TransformBy(InstanceTransform);
		if (Intersect(WorldActorEditorBox))
		{
			FWorldPartitionHandle ActorHandle(InWorldPartition, ActorDesc->GetGuid());
			InOperation(ActorHandle);

			if (ActorHandle->GetIsSpatiallyLoaded() && ActorHandle->IsContainerInstance())
			{
				FWorldPartitionActorDesc::FContainerInstance ContainerInstance;
				if (ActorHandle->GetContainerInstance(ContainerInstance))
				{
					if (ContainerInstance.bSupportsPartialEditorLoading)
					{
						if (UWorldPartition* ContainerWorldPartition = ContainerInstance.LoadedLevel ? ContainerInstance.LoadedLevel->GetWorldPartition() : nullptr)
						{
							HandleIntersectingContainer(ContainerWorldPartition, InOperation);
						}
					}
				}
			}
		}
	}, ForEachIntersectingActorParams);
}
#endif
