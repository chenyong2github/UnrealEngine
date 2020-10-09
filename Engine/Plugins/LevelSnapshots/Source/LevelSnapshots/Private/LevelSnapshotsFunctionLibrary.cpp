// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsFunctionLibrary.h"
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