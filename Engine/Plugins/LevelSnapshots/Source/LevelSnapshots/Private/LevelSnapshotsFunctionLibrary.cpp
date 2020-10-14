// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsFunctionLibrary.h"
#include "DiffUtils.h"
#include "Engine/LevelStreaming.h"
#include "LevelSnapshot.h"

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

void ULevelSnapshotsFunctionLibrary::TestDeserialization(const ULevelSnapshot* Snapshot, AActor* TestActor)
{
	if (!TestActor)
	{
		return;
	}

	if (Snapshot && Snapshot->ActorSnapshots.Num() > 0)
	{
		for (const TPair<FString, FActorSnapshot>& SnapshotPair : Snapshot->ActorSnapshots)
		{
			const FString& SnapshotPathName = SnapshotPair.Key;
			const FActorSnapshot& ActorSnapshot = SnapshotPair.Value;

			// See if the Snapshot is for the same actor
			if (SnapshotPathName == TestActor->GetPathName())
			{
				AActor* DeserializedActor = ActorSnapshot.GetDeserializedActor();
				if (DeserializedActor)
				{
					TArray<FSingleObjectDiffEntry> DifferingProperties;
					DiffUtils::CompareUnrelatedObjects(DeserializedActor, TestActor, DifferingProperties);
					for (FSingleObjectDiffEntry& DifferingProperty : DifferingProperties)
					{
						FString PropertyDisplayName = DifferingProperty.Identifier.ToDisplayName();
						UE_LOG(LogTemp, Warning, TEXT("Property Difference: %s"), *PropertyDisplayName);
					}
				}
				break;
			}
		}
	}
}
