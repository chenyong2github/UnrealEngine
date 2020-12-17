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
#if WITH_DEV_AUTOMATION_TESTS
	if (GIsAutomationTesting)
	{
		return ActorDesc->GetHardRefCount() > 0;
	}
#endif

	return !!ActorDesc->GetActor();
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
		check(!IsEngineExitRequested());

		if (Actor)
		{
			ActorDesc->RegisterActor();
		}
	}
}

void FWorldPartitionReferenceImpl::DecRefCount(FWorldPartitionActorDesc* ActorDesc)
{
	if (!ActorDesc->DecHardRefCount())
	{
		if (!IsEngineExitRequested())
		{
			// We can still hold a reference to an actor that was manually deleted in the editor
			if (AActor* Actor = ActorDesc->GetActor())
			{
				ActorDesc->UnregisterActor();
				ActorDesc->Unload();
			}
		}
	}
}
#endif