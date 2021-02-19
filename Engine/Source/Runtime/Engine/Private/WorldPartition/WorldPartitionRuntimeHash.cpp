// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionRuntimeHash.h"

UWorldPartitionRuntimeHash::UWorldPartitionRuntimeHash(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{}

#if WITH_EDITOR
void UWorldPartitionRuntimeHash::OnPreBeginPIE()
{
	// Mark always loaded actors so that the Level will force reference to these actors for PIE.
	// These actor will then be duplicated for PIE during the PIE world duplication process
	ForceExternalActorLevelReference(/*bForceExternalActorLevelReferenceForPIE*/true);
}

void UWorldPartitionRuntimeHash::OnEndPIE()
{
	// Unmark always loaded actors
	ForceExternalActorLevelReference(/*bForceExternalActorLevelReferenceForPIE*/false);

	// Release references (will unload actors that were not already loaded in the Editor)
	AlwaysLoadedActorsForPIE.Empty();
}

void UWorldPartitionRuntimeHash::ForceExternalActorLevelReference(bool bForceExternalActorLevelReferenceForPIE)
{
	for (const FWorldPartitionReference& AlwaysLoadedActor : AlwaysLoadedActorsForPIE)
	{
		if (AActor* Actor = (*AlwaysLoadedActor)->GetActor())
		{
			Actor->SetForceExternalActorLevelReferenceForPIE(bForceExternalActorLevelReferenceForPIE);
		}
	}
}

#endif
