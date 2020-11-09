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

		// For now only snapshot the actors which would be visible in the scene outliner to avoid complications with special hidden actors
		if (Actor->IsListedInSceneOutliner())
		{

			UE_LOG(LogTemp, Warning, TEXT("Found Valid Object - %s"), *Actor->GetPathName());
			SnapshotActor(Actor);
		}
	}

}

void ULevelSnapshot::SnapshotActor(AActor* TargetActor)
{
	FLevelSnapshot_Actor& NewSnapshot = ActorSnapshots.Add(TargetActor->GetPathName(), FLevelSnapshot_Actor(TargetActor));
}
