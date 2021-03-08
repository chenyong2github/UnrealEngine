// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/ActorDescList.h"

#if WITH_EDITOR
FWorldPartitionActorDesc* FActorDescList::AddActor(const AActor* InActor)
{
	TUniquePtr<FWorldPartitionActorDesc>* NewActorDesc = new(ActorDescList) TUniquePtr<FWorldPartitionActorDesc>(InActor->CreateActorDesc());
	check(NewActorDesc->IsValid());

	check(!Actors.Contains((*NewActorDesc)->GetGuid()));
	Actors.Add((*NewActorDesc)->GetGuid(), NewActorDesc);

	return NewActorDesc->Get();
}

const FWorldPartitionActorDesc* FActorDescList::GetActorDesc(const FGuid& Guid) const
{
	const TUniquePtr<FWorldPartitionActorDesc>* const * ActorDesc = Actors.Find(Guid);
	return ActorDesc ? (*ActorDesc)->Get() : nullptr;
}

FWorldPartitionActorDesc* FActorDescList::GetActorDesc(const FGuid& Guid)
{
	TUniquePtr<FWorldPartitionActorDesc>** ActorDesc = Actors.Find(Guid);
	return ActorDesc ? (*ActorDesc)->Get() : nullptr;
}

const FWorldPartitionActorDesc& FActorDescList::GetActorDescChecked(const FGuid& Guid) const
{
	const TUniquePtr<FWorldPartitionActorDesc>* const ActorDesc = Actors.FindChecked(Guid);
	return *ActorDesc->Get();
}

FWorldPartitionActorDesc& FActorDescList::GetActorDescChecked(const FGuid& Guid)
{
	TUniquePtr<FWorldPartitionActorDesc>* ActorDesc = Actors.FindChecked(Guid);
	return *ActorDesc->Get();
}

const FWorldPartitionActorDesc* FActorDescList::GetActorDesc(const FString& PackageName) const
{
	const FName PackageFName(*PackageName);
	for (const TUniquePtr<FWorldPartitionActorDesc>& ActorDescPtr : ActorDescList)
	{
		if (ActorDescPtr->GetActorPackage() == PackageFName)
		{
			return ActorDescPtr.Get();
		}
	}

	return nullptr;
}

void FActorDescList::Empty()
{
	Actors.Empty();
	ActorDescList.Empty();
}
#endif