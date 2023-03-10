// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/LoaderAdapter/LoaderAdapterPinnedActors.h"

#if WITH_EDITOR

#include "Engine/World.h"
#include "Engine/Level.h"
#include "WorldPartition/WorldPartition.h"

#define LOCTEXT_NAMESPACE "FLoaderAdapterPinnedActors"

bool FLoaderAdapterPinnedActors::PassActorDescFilter(const FWorldPartitionHandle& ActorHandle) const
{
	// We want to be able to pin any type of actors (HLODs, etc).
	return ActorHandle.IsValid() && !ActorsToRemove.Contains(ActorHandle);
}

bool FLoaderAdapterPinnedActors::SupportsPinning(FWorldPartitionActorDesc* InActorDesc)
{
	if (!InActorDesc)
	{
		return false;
	}

	// Only Spatially loaded actors can be pinned with the exception of non spatially loaded, runtime only actors (ex: HLODs)
	if (!InActorDesc->GetIsSpatiallyLoaded() && !InActorDesc->GetActorIsRuntimeOnly())
	{
		return false;
	}

	if (UActorDescContainer* Container = InActorDesc->GetContainer())
	{
		if (Container->IsMainPartitionContainer())
		{
			return true;
		}
		else if (InActorDesc->GetContentBundleGuid().IsValid())
		{
			const UWorldPartition* ContainerWorldPartition = Container->GetWorldPartition();
			if (ContainerWorldPartition && ContainerWorldPartition->IsMainWorldPartition())
			{
				return InActorDesc->GetActorSoftPath().GetAssetPath().GetPackageName() == ContainerWorldPartition->GetTypedOuter<UWorld>()->GetPackage()->GetFName();
			}
		}
	}

	return false;
}

bool FLoaderAdapterPinnedActors::SupportsPinning(AActor* InActor)
{
	if (InActor)
	{
		// Pinning of Actors is only supported on the main world partition
		const ULevel* Level = InActor->GetLevel();
		const UWorld* World = Level->GetWorld();

		// Only Spatially loaded actors can be pinned with the exception of non spatially loaded, runtime only actors (ex: HLODs)
		return World && !World->IsGameWorld() && !!World->GetWorldPartition() && Level->IsPersistentLevel() && InActor->IsPackageExternal() && (InActor->GetIsSpatiallyLoaded() || InActor->IsRuntimeOnly());
	}

	return false;
}

#undef LOCTEXT_NAMESPACE

#endif
