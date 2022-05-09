// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR

class UWorld;
class UWorldPartition;
class FWorldPartitionActorDesc;

class ENGINE_API FWorldPartitionHelpers
{
	static UClass* ResolveActorDescClass(const FWorldPartitionActorDesc* ActorDesc);

public:
	template <class ActorClass = AActor>
	static void ForEachIntersectingActorDesc(UWorldPartition* WorldPartition, const FBox& Box, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func)
	{
		ForEachIntersectingActorDesc(WorldPartition, Box, ActorClass::StaticClass(), Func);
	}

	template<class ActorClass = AActor>
	static void ForEachActorDesc(UWorldPartition* WorldPartition, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func)
	{
		ForEachActorDesc(WorldPartition, ActorClass::StaticClass(), Func);
	}

	static void ForEachIntersectingActorDesc(UWorldPartition* WorldPartition, const FBox& Box, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func);
	static void ForEachActorDesc(UWorldPartition* WorldPartition, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func);

	/* Struct of optional parameters passed to foreach actordesc functions. */
	struct ENGINE_API FForEachActorWithLoadiongParams
	{
		FForEachActorWithLoadiongParams();

		/* Perform a garbage collection per-actor, useful to test if the caller properly handle GCs. */
		bool bGCPerActor;

		/* The class used to filter actors loading. */
		TSubclassOf<AActor> ActorClass;

		/* Custom filter function used to filter actors loading. */
		TUniqueFunction<bool(const FWorldPartitionActorDesc*)> FilterActorDesc;

		/* Called right before releasing actor references and performing garbage collection. */
		TUniqueFunction<void()> OnPreGarbageCollect;
	};

	template<class ActorClass = AActor>
	static void ForEachActorWithLoading(UWorldPartition* WorldPartition, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func)
	{
		ForEachActorWithLoading(WorldPartition, ActorClass::StaticClass(), Func);
	}

	static void ForEachActorWithLoading(UWorldPartition* WorldPartition, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func, TFunctionRef<void()> OnReleasingActorReferences = [](){}, bool bGCPerActor = false);
	static void ForEachActorWithLoading(UWorldPartition* WorldPartition, const TArray<FGuid>& ActorGuids, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func, TFunctionRef<void()> OnReleasingActorReferences = [](){}, bool bGCPerActor = false);
	static void ForEachActorWithLoading(UWorldPartition* WorldPartition, const FForEachActorWithLoadiongParams& Params, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func);
	
	static bool HasExceededMaxMemory();
	static void DoCollectGarbage();

	// Simulate an engine frame tick
	static void FakeEngineTick(UWorld* World);
};

#endif // WITH_EDITOR
