// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/ActorDescContainer.h"

#if WITH_EDITOR
TUniquePtr<FWorldPartitionActorDesc>* FWorldPartitionHandleUtils::GetActorDesc(UActorDescContainer* Container, const FGuid& ActorGuid)
{
	if (TUniquePtr<FWorldPartitionActorDesc>** ActorDescPtr = Container->ActorsByGuid.Find(ActorGuid))
	{
		return *ActorDescPtr;
	}

	return nullptr;
}

UActorDescContainer* FWorldPartitionHandleUtils::GetActorDescContainer(TUniquePtr<FWorldPartitionActorDesc>* ActorDesc)
{
	return ActorDesc ? ActorDesc->Get()->GetContainer() : nullptr;
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
		TGuardValue<bool> IsEditorLoadingPackageGuard(GIsEditorLoadingPackage, true);
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