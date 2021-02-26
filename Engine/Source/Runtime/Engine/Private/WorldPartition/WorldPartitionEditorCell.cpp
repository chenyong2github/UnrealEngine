// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionEditorCell.h"
#include "WorldPartition/WorldPartitionEditorHash.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

void UWorldPartitionEditorCell::Serialize(FArchive& Ar)
{
#if WITH_EDITOR
	if (Ar.IsTransacting())
	{
		bool bLoadedBool = bLoaded;
		Ar << bLoadedBool;
		bLoaded = bLoadedBool;
	}
#endif

	Super::Serialize(Ar);
}

#if WITH_EDITOR
void UWorldPartitionEditorCell::BeginDestroy()
{
	// Release WorldPartition Actor Handles/References
	Actors.Empty();
	LoadedActors.Empty();
	Super::BeginDestroy();
}

void UWorldPartitionEditorCell::AddActor(const FWorldPartitionHandle& ActorHandle)
{
	AddActor(ActorHandle->GetGuid(), ActorHandle);
}

void UWorldPartitionEditorCell::AddActor(const FGuid& Source, const FWorldPartitionHandle& ActorHandle)
{
	check(ActorHandle.IsValid());
	
	bool bIsAlreadyInSet = false;
	Actors.Add(FActorHandle(Source, ActorHandle), &bIsAlreadyInSet);

	if (!bIsAlreadyInSet)
	{	
		if (ActorHandle.IsLoaded())
		{
			LoadedActors.Add(FActorReference(Source, ActorHandle), &bIsAlreadyInSet);
			check(!bIsAlreadyInSet);
		}

		UWorldPartition* WorldPartition = GetTypedOuter<UWorldPartition>();
		for (const FGuid& ReferenceGuid : ActorHandle->GetReferences())
		{
			FWorldPartitionHandle ReferenceHandle(WorldPartition, ReferenceGuid);
			AddActor(ActorHandle->GetGuid(), ReferenceHandle);
		}
	}
}

void UWorldPartitionEditorCell::RemoveActor(const FWorldPartitionHandle& ActorHandle)
{
	RemoveActor(ActorHandle->GetGuid(), ActorHandle);
}

void UWorldPartitionEditorCell::RemoveActor(const FGuid& Source, const FWorldPartitionHandle& ActorHandle)
{
	check(ActorHandle.IsValid());
	
	if (Actors.Remove(FActorHandle(Source, ActorHandle)))
	{
		// Don't call LoadedActors.Remove(ActorHandle) right away, as this will create a temporary reference and might try to load
		// a deleted actor. This is a temporary workaround.
		for (const FActorReference& ActorReference: LoadedActors)
		{
			if ((ActorReference.Source == Source) && (ActorReference.Handle == ActorHandle))
			{
				LoadedActors.Remove(FActorReference(Source, ActorHandle));
				break;
			}
		}

		UWorldPartition* WorldPartition = GetTypedOuter<UWorldPartition>();
		for (const FGuid& ReferenceGuid : ActorHandle->GetReferences())
		{
			FWorldPartitionHandle ReferenceHandle(WorldPartition, ReferenceGuid);
			RemoveActor(ActorHandle->GetGuid(), ReferenceHandle);
		}
	}
}
#endif

UWorldPartitionEditorCell::UWorldPartitionEditorCell(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, Bounds(ForceInitToZero)
	, bLoaded(false)
#endif
{}
