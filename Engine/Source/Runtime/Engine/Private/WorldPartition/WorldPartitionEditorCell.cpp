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
void UWorldPartitionEditorCell::AddActor(FWorldPartitionActorDesc* InActorDesc)
{
	check(InActorDesc);
	bool bIsAlreadyInSet = false;
	Actors.Add(InActorDesc, &bIsAlreadyInSet);

	if (!bIsAlreadyInSet && bLoaded)
	{
		if (AActor* Actor = InActorDesc->GetActor())
		{
			check(!Actor->IsChildActor());

			bIsAlreadyInSet = false;
			LoadedActors.Add(InActorDesc, &bIsAlreadyInSet);
			check(!bIsAlreadyInSet);

			const uint32 ActorRefCount = InActorDesc->IncHardRefCount();
			UE_LOG(LogWorldPartition, Verbose, TEXT(" ==> Referenced loaded actor %s(%d) [UWorldPartitionEditorCell::AddActor]"), *Actor->GetFullName(), ActorRefCount);
		}
	}
}

void UWorldPartitionEditorCell::RemoveActor(FWorldPartitionActorDesc* InActorDesc)
{
	check(InActorDesc);
	verify(Actors.Remove(InActorDesc));

	if (LoadedActors.Remove(InActorDesc))
	{
		check(bLoaded);

		const uint32 ActorRefCount = InActorDesc->DecHardRefCount();
		UE_LOG(LogWorldPartition, Verbose, TEXT(" ==> Unreferenced loaded actor %s(%d) [UWorldPartitionEditorCell::RemoveActor]"), *InActorDesc->GetActor()->GetFullName(), ActorRefCount);
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
