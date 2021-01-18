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
		AActor* Actor = ActorDesc->Load();
		check(Actor || GIsAutomationTesting);

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
		// The only case where an actor can be unloaded while still holding a reference 
		// is when it was manually deleted in the editor.
		if (ActorDesc->IsLoaded())
		{
			ActorDesc->UnregisterActor();
			ActorDesc->Unload();
		}
	}
}
#endif