// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshot.h"
#include "EngineUtils.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/LevelStreaming.h"

void ULevelSnapshot::SnapshotWorld(UWorld* TargetWorld)
{
	if (!TargetWorld)
	{
		UE_LOG(LogTemp, Warning, TEXT("Unable To Snapshot World as World was invalid"));
	}

	MapPath = FSoftObjectPath(TargetWorld);
	ActorSnapshots.Empty(); // If we keep calling this method on the same asset it will add the same actors over and over unless we empty

	UE_LOG(LogTemp, Warning, TEXT("Attempting to Snapshot World - %s"), *TargetWorld->GetMapName());

	for (TActorIterator<AActor> It(TargetWorld, AActor::StaticClass(), EActorIteratorFlags::SkipPendingKill); It; ++It)
	{
		AActor* Actor = *It;

		// For now only snapshot the actors which would be visible in the scene outliner to avoid complications with special hidden actors
		// We'll also filter out actors of specific classes
		if (Actor->IsListedInSceneOutliner() && DoesActorHaveSupportedClass(Actor))
		{

			UE_LOG(LogTemp, Warning, TEXT("Found Valid Object - %s"), *Actor->GetPathName());
			SnapshotActor(Actor);
		}
	}

}

bool ULevelSnapshot::DoesActorHaveSupportedClass(const AActor* Actor)
{
	const UClass* ActorClass = Actor->GetClass();

	const TArray<UClass*> UnsupportedClasses = 
	{
		ALevelScriptActor::StaticClass(), // The level blueprint. Filtered out to avoid external map errors when saving a snapshot.
		ABrush::StaticClass() // Brush Actors
	};

	for (UClass* Class : UnsupportedClasses)
	{
		if (Actor->IsA(Class))
		{
			return false;
		}
	}
	
	return true;
}

void ULevelSnapshot::SnapshotActor(AActor* TargetActor)
{
	FLevelSnapshot_Actor& NewSnapshot = ActorSnapshots.Add(TargetActor->GetPathName(), FLevelSnapshot_Actor(TargetActor));
}
