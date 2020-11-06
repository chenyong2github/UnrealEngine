// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshot.h"
#include "EngineUtils.h"
#include "Engine/LevelStreaming.h"

void ULevelSnapshot::SnapshotWorld(UWorld* TargetWorld)
{
	if (!TargetWorld)
	{
		UE_LOG(LogTemp, Warning, TEXT("Unable To Snapshot World as World was invalid"));
	}

	MapName = TargetWorld->GetMapName();

	UE_LOG(LogTemp, Warning, TEXT("Attempting to Snapshot World - %s"), *TargetWorld->GetMapName());

	for (TActorIterator<AActor> It(TargetWorld, AActor::StaticClass(), EActorIteratorFlags::SkipPendingKill); It; ++It)
	{
		AActor* Actor = *It;

		UE_LOG(LogTemp, Warning, TEXT("Found Valid Object - %s"), *Actor->GetPathName());
		if (Actor->IsA(UWorld::StaticClass()))
		{
			continue;
		}
		SnapshotActor(Actor);
	}
}

void ULevelSnapshot::SnapshotActor(AActor* TargetActor)
{
	ActorSnapshots.Add(TargetActor->GetPathName(), FActorSnapshot(TargetActor));
}
