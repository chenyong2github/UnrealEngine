// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "SnapshotDataCache.generated.h"

USTRUCT()
struct FActorSnapshotCache
{
	GENERATED_BODY()
	
	UPROPERTY()
	TWeakObjectPtr<AActor> CachedSnapshotActor = nullptr;

	/**
	 * Whether we already serialised the snapshot data into the actor.
	 * 
	 * This exists because sometimes we need to preallocate an actor without serialisation.
	 * Example: When serializing another actor which referenced this actor.
	 */
	UPROPERTY()
	bool bReceivedSerialisation = false;

	/**
	 * Stores all object dependencies. Only valid if bReceivedSerialisation == true.
	 */
	UPROPERTY()
	TArray<int32> ObjectDependencies;
};

USTRUCT()
struct FSubobjectSnapshotCache
{
	GENERATED_BODY()
	
	/** Allocated in snapshot world */
	UPROPERTY()
	TObjectPtr<UObject> SnapshotObject = nullptr;

	/** Allocated in editor world */
	UPROPERTY()
	TWeakObjectPtr<UObject> EditorObject = nullptr;
};

USTRUCT()
struct FClassDefaultSnapshotCache
{
	GENERATED_BODY()

	UPROPERTY()
	UObject* CachedLoadedClassDefault = nullptr;
};

/** Caches data for re-use. */
USTRUCT()
struct FSnapshotDataCache
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FSoftObjectPath, FActorSnapshotCache> ActorCache;

	UPROPERTY()
	TMap<FSoftObjectPath, FSubobjectSnapshotCache> SubobjectCache;

	UPROPERTY()
	TMap<FSoftClassPath, FClassDefaultSnapshotCache> ClassDefaultCache;
};
