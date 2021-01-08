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
	check(ActorHandle.IsValid());
	
	bool bIsAlreadyInSet = false;
	Actors.Add(ActorHandle, &bIsAlreadyInSet);
	check(!bIsAlreadyInSet);

	if (ActorHandle.IsLoaded())
	{
		bIsAlreadyInSet = false;
		LoadedActors.Add(ActorHandle, &bIsAlreadyInSet);
		check(!bIsAlreadyInSet);
	}
}

void UWorldPartitionEditorCell::RemoveActor(const FWorldPartitionHandle& ActorHandle)
{
	check(ActorHandle.IsValid());
	verify(Actors.Remove(ActorHandle));
	LoadedActors.Remove(ActorHandle);
}
#endif

UWorldPartitionEditorCell::UWorldPartitionEditorCell(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, Bounds(ForceInitToZero)
	, bLoaded(false)
#endif
{}
