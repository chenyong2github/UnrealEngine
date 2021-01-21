// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
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
	return ActorDesc->IsLoaded();
}

void FWorldPartitionHandleImpl::IncRefCount(FWorldPartitionActorDesc* ActorDesc)
{
	ActorDesc->IncSoftRefCount();
}

void FWorldPartitionHandleImpl::DecRefCount(FWorldPartitionActorDesc* ActorDesc)
{
	ActorDesc->DecSoftRefCount();
}

void FWorldPartitionReferenceImpl::IncRefCount(FWorldPartitionActorDesc* ActorDesc)
{
	if (ActorDesc->IncHardRefCount() == 1)
	{
		ActorDesc->Load();
		ActorDesc->RegisterActor();
	}
}

void FWorldPartitionReferenceImpl::DecRefCount(FWorldPartitionActorDesc* ActorDesc)
{
	if (ActorDesc->DecHardRefCount() == 0)
	{
		ActorDesc->UnregisterActor();
		ActorDesc->Unload();
	}
}
#endif