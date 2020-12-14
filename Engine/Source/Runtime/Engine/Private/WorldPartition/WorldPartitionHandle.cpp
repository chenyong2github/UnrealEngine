// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartition.h"

#if WITH_EDITOR
TUniquePtr<FWorldPartitionActorDesc>* FWorldPartitionHandleBase::GetActorDesc(UWorldPartition* WorldPartition, const FGuid& ActorGuid)
{
	if (TUniquePtr<FWorldPartitionActorDesc>** ActorDescPtr = WorldPartition->Actors.Find(ActorGuid))
	{
		return *ActorDescPtr;
	}

	return nullptr;
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
		AActor* Actor = ActorDesc->Load();
		check(Actor || GIsAutomationTesting);

		if (Actor)
		{
			Actor->GetLevel()->AddLoadedActor(Actor);
		}
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