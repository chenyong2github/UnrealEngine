// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/ActorDescList.h"

#if WITH_EDITOR
FWorldPartitionActorDesc* FActorDescList::AddActor(const AActor* InActor)
{
	FWorldPartitionActorDesc* NewActorDesc = InActor->CreateActorDesc().Release();
	check(NewActorDesc);

	AddActorDescriptor(NewActorDesc);

	return NewActorDesc;
}

const FWorldPartitionActorDesc* FActorDescList::GetActorDesc(const FGuid& Guid) const
{
	const TUniquePtr<FWorldPartitionActorDesc>* const * ActorDesc = ActorsByGuid.Find(Guid);
	return ActorDesc ? (*ActorDesc)->Get() : nullptr;
}

FWorldPartitionActorDesc* FActorDescList::GetActorDesc(const FGuid& Guid)
{
	TUniquePtr<FWorldPartitionActorDesc>** ActorDesc = ActorsByGuid.Find(Guid);
	return ActorDesc ? (*ActorDesc)->Get() : nullptr;
}

const FWorldPartitionActorDesc& FActorDescList::GetActorDescChecked(const FGuid& Guid) const
{
	const TUniquePtr<FWorldPartitionActorDesc>* const ActorDesc = ActorsByGuid.FindChecked(Guid);
	return *ActorDesc->Get();
}

FWorldPartitionActorDesc& FActorDescList::GetActorDescChecked(const FGuid& Guid)
{
	TUniquePtr<FWorldPartitionActorDesc>* ActorDesc = ActorsByGuid.FindChecked(Guid);
	return *ActorDesc->Get();
}

const FWorldPartitionActorDesc* FActorDescList::GetActorDesc(const FString& PackageName) const
{
	FString ActorName;
	FString ActorContext;
	if (!PackageName.Split(TEXT("."), &ActorContext, &ActorName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
	{
		ActorName = PackageName;
	}

	if (const TUniquePtr<FWorldPartitionActorDesc>* const* ActorDesc = ActorsByName.Find(*ActorName))
	{
		return (*ActorDesc)->Get();
	}

	return nullptr;
}

const FWorldPartitionActorDesc* FActorDescList::GetActorDesc(const FSoftObjectPath& InActorPath) const
{
	const FName ActorPath(*InActorPath.ToString());
	for (const TUniquePtr<FWorldPartitionActorDesc>& ActorDescPtr : ActorDescList)
	{
		if (ActorDescPtr && ActorDescPtr->GetActorPath() == ActorPath)
		{
			return ActorDescPtr.Get();
		}
	}

	return nullptr;
}

void FActorDescList::Empty()
{
	ActorsByGuid.Empty();
	ActorsByName.Empty();
	ActorDescList.Empty();
}

void FActorDescList::AddActorDescriptor(FWorldPartitionActorDesc* ActorDesc)
{
	check(ActorDesc);
	TUniquePtr<FWorldPartitionActorDesc>* NewActorDesc = new(ActorDescList) TUniquePtr<FWorldPartitionActorDesc>(ActorDesc);
	ActorsByGuid.Add(ActorDesc->GetGuid(), NewActorDesc);
	ActorsByName.Add(*ActorDesc->GetActorName().ToString(), NewActorDesc);
}

void FActorDescList::RemoveActorDescriptor(FWorldPartitionActorDesc* ActorDesc)
{
	check(ActorDesc);
	verify(ActorsByGuid.Remove(ActorDesc->GetGuid()));
	verify(ActorsByName.Remove(*ActorDesc->GetActorName().ToString()));
}

TUniquePtr<FWorldPartitionActorDesc>* FActorDescList::GetActorDescriptor(const FGuid& ActorGuid)
{
	return ActorsByGuid.FindRef(ActorGuid);
}
#endif