// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsFunctionLibrary.h"
#include "DiffUtils.h"
#include "EngineUtils.h"
#include "Engine/LevelStreaming.h"
#include "LevelSnapshot.h"
#include "LevelSnapshotFilters.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

ULevelSnapshot* ULevelSnapshotsFunctionLibrary::TakeLevelSnapshot(const UObject* WorldContextObject)
{
	UWorld* TargetWorld = nullptr;
	if (WorldContextObject)
	{
		TargetWorld = WorldContextObject->GetWorld();
	}

	if (TargetWorld)
	{
		UE_LOG(LogTemp, Warning, TEXT("Snapshot taken in World Type - %d"), TargetWorld->WorldType);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Snapshot taken with no valid World set"));
		return nullptr;
	}

	FName NewSnapshotName(TEXT("Snapshot"));
	ULevelSnapshot* NewSnapshot = NewObject<ULevelSnapshot>(GetTransientPackage(), NewSnapshotName);

	NewSnapshot->SnapshotWorld(TargetWorld);

	return NewSnapshot;
};

void ULevelSnapshotsFunctionLibrary::ApplySnapshotToWorld(const UObject* WorldContextObject, const ULevelSnapshot* Snapshot, const ULevelSnapshotFilter* Filter /*= nullptr*/)
{
	UWorld* TargetWorld = nullptr;
	if (WorldContextObject)
	{
		TargetWorld = WorldContextObject->GetWorld();
	}

	if (!TargetWorld)
	{
		return;
	}

	for (TActorIterator<AActor> It(TargetWorld, AActor::StaticClass(), EActorIteratorFlags::SkipPendingKill); It; ++It)
	{
		AActor* Actor = *It;
		// For now only snapshot the actors which would be visible in the scene outliner to avoid complications with special hidden actors
		if (!Actor->IsListedInSceneOutliner())
		{
			continue;
		}

		for (const TPair<FString, FLevelSnapshot_Actor>& SnapshotPair : Snapshot->ActorSnapshots)
		{
			const FString& SnapshotPathName = SnapshotPair.Key;
			const FLevelSnapshot_Actor& ActorSnapshot = SnapshotPair.Value;

			// See if the Snapshot is for the same actor
			if (SnapshotPathName == Actor->GetPathName())
			{
				ActorSnapshot.Deserialize(Actor);
				break;
			}
		}
	}
}

void PrintObjectDifferences(const AActor* A, const AActor* B)
{
	if (!A || !B)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("\t--Calculating Differences--"));

	TArray<FSingleObjectDiffEntry> DifferingProperties;
	DiffUtils::CompareUnrelatedObjects(A, B, DifferingProperties);
	for (FSingleObjectDiffEntry& DifferingProperty : DifferingProperties)
	{
		FString PropertyDisplayName = DifferingProperty.Identifier.ToDisplayName();
		UE_LOG(LogTemp, Warning, TEXT("\tProperty Difference: %s"), *PropertyDisplayName);
	}
}

void ULevelSnapshotsFunctionLibrary::TestDeserialization(const ULevelSnapshot* Snapshot, AActor* TestActor)
{
	if (!TestActor)
	{
		return;
	}

	if (Snapshot && Snapshot->ActorSnapshots.Num() > 0)
	{
		for (const TPair<FString, FLevelSnapshot_Actor>& SnapshotPair : Snapshot->ActorSnapshots)
		{
			const FString& SnapshotPathName = SnapshotPair.Key;
			const FLevelSnapshot_Actor& ActorSnapshot = SnapshotPair.Value;

			// See if the Snapshot is for the same actor
			if (SnapshotPathName == TestActor->GetPathName())
			{
				UE_LOG(LogTemp, Warning, TEXT("Found matching snapshot!"));

				UE_LOG(LogTemp, Warning, TEXT("\tOld Transform: %s"), *TestActor->GetActorLocation().ToString());
				ActorSnapshot.Deserialize(TestActor);
				UE_LOG(LogTemp, Warning, TEXT("\tNew Transform: %s"), *TestActor->GetActorLocation().ToString());
			}
		}
	}
}

void ULevelSnapshotsFunctionLibrary::DiffSnapshots(const ULevelSnapshot* FirstSnapshot, const ULevelSnapshot* SecondSnapshot)
{
	if (!FirstSnapshot || !SecondSnapshot)
	{
		UE_LOG(LogTemp, Warning, TEXT("Unable to Diff snapshots as at least one snapshot was invalid"));
		return;
	}
	
	for (const TPair<FString, FLevelSnapshot_Actor>& FirstSnapshotPair : FirstSnapshot->ActorSnapshots)
	{
		const FString& FirstSnapshotPathName = FirstSnapshotPair.Key;
		const FLevelSnapshot_Actor& FirstActorSnapshot = FirstSnapshotPair.Value;

		if (const FLevelSnapshot_Actor* SecondActorSnapshot = SecondSnapshot->ActorSnapshots.Find(FirstSnapshotPathName))
		{
			UE_LOG(LogTemp, Warning, TEXT("Found Matching Actor: %s"), *FirstSnapshotPathName);

			AActor* FirstActor = FirstActorSnapshot.GetDeserializedActor();
			AActor* SecondActor = SecondActorSnapshot->GetDeserializedActor();

			if (FirstActor && SecondActor)
			{
				PrintObjectDifferences(FirstActor, SecondActor);
				FirstActor->Destroy();
				SecondActor->Destroy();
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("%s exists in the First snapshot but not the Second."), *FirstSnapshotPathName);
		}
	}

	for (const TPair<FString, FLevelSnapshot_Actor>& SecondSnapshotPair : SecondSnapshot->ActorSnapshots)
	{
		const FString& SecondSnapshotPathName = SecondSnapshotPair.Key;
		const FLevelSnapshot_Actor& SecondActorSnapshot = SecondSnapshotPair.Value;

		if (!FirstSnapshot->ActorSnapshots.Find(SecondSnapshotPathName))
		{
			UE_LOG(LogTemp, Warning, TEXT("%s exists in the Second snapshot but not the First."), *SecondSnapshotPathName);
		}
	}
}
