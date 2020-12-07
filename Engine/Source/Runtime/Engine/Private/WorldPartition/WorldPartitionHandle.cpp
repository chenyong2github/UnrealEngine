// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionHandle.h"

#if WITH_EDITOR
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
		AActor* Actor = ActorDesc->Load();
		Actor->GetLevel()->AddLoadedActor(Actor);
	}
}

void FWorldPartitionReferenceImpl::DecRefCount(FWorldPartitionActorDesc* ActorDesc)
{
	if (!ActorDesc->DecHardRefCount())
	{
		if (AActor* Actor = ActorDesc->GetActor())
		{
			Actor->GetLevel()->RemoveLoadedActor(Actor);
			ActorDesc->Unload();
		}
	}
}

FWorldPartitionReference FWorldPartitionHandleHelpers::ConvertHandleToReference(const FWorldPartitionHandle& Handle)
{
	FWorldPartitionReference Reference;

	if (Handle.IsValid())
	{
		Reference.ActorDesc = Handle.ActorDesc;
		Reference.IncRefCount();
	}

	return Reference;
}

FWorldPartitionHandle FWorldPartitionHandleHelpers::ConvertReferenceToHandle(const FWorldPartitionReference& Reference)
{
	FWorldPartitionHandle Handle;

	if (Reference.IsValid())
	{
		Handle.ActorDesc = Reference.ActorDesc;
		Handle.IncRefCount();
	}

	return Handle;
}
#endif