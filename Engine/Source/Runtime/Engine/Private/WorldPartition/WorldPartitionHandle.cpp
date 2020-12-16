// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartition.h"

#if WITH_EDITOR
TUniquePtr<FWorldPartitionActorDesc>* FWorldPartitionHandleUtils::GetActorDesc(UWorldPartition* WorldPartition, const FGuid& ActorGuid)
{
	if (TUniquePtr<FWorldPartitionActorDesc>** ActorDescPtr = WorldPartition->Actors.Find(ActorGuid))
	{
		return *ActorDescPtr;
	}

	return nullptr;
}

bool FWorldPartitionHandleUtils::IsActorDescLoaded(FWorldPartitionActorDesc* ActorDesc)
{
	return !!ActorDesc->GetActor();
}

void FWorldPartitionSoftRefImpl::IncRefCount(FWorldPartitionActorDesc* ActorDesc)
{
	ActorDesc->IncSoftRefCount();
}

void FWorldPartitionSoftRefImpl::DecRefCount(FWorldPartitionActorDesc* ActorDesc)
{
	ActorDesc->DecSoftRefCount();
}

void FWorldPartitionHardRefImpl::IncRefCount(FWorldPartitionActorDesc* ActorDesc)
{
	if (ActorDesc->IncHardRefCount() == 1)
	{
		AActor* Actor = ActorDesc->Load();
		check(Actor || GIsAutomationTesting);
		check(!IsEngineExitRequested());

		if (Actor)
		{
			Actor->GetLevel()->AddLoadedActor(Actor);
		}
	}
}

void FWorldPartitionHardRefImpl::DecRefCount(FWorldPartitionActorDesc* ActorDesc)
{
	if (!ActorDesc->DecHardRefCount())
	{
		if (!IsEngineExitRequested())
		{
			if (AActor* Actor = ActorDesc->GetActor())
			{
				Actor->GetLevel()->RemoveLoadedActor(Actor);
				ActorDesc->Unload();
			}
		}
	}
}

void FWorldPartitionPinRefImpl::IncRefCount(FWorldPartitionActorDesc* ActorDesc)
{
	bIsReference = ActorDesc->GetHardRefCount() > 0;

	if (bIsReference)
	{
		FWorldPartitionHardRefImpl::IncRefCount(ActorDesc);
	}
	else
	{
		FWorldPartitionSoftRefImpl::IncRefCount(ActorDesc);
	}
}

void FWorldPartitionPinRefImpl::DecRefCount(FWorldPartitionActorDesc* ActorDesc)
{
	if (bIsReference)
	{
		FWorldPartitionHardRefImpl::DecRefCount(ActorDesc);
	}
	else
	{
		FWorldPartitionSoftRefImpl::DecRefCount(ActorDesc);
	}
}
#endif