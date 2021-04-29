// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR

class UWorldPartition;
class FWorldPartitionActorDesc;

class ENGINE_API FWorldPartitionHelpers
{
public:
	template <class ActorClass = AActor>
	static void ForEachIntersectingActorDesc(UWorldPartition* WorldPartition, const FBox& Box, bool bIncludeFromChildActors, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func)
	{
		ForEachIntersectingActorDesc(WorldPartition, Box, bIncludeFromChildActors, ActorClass::StaticClass(), Func);
	}

	template<class ActorClass = AActor>
	static void ForEachActorDesc(UWorldPartition* WorldPartition, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func)
	{
		ForEachActorDesc(WorldPartition, ActorClass::StaticClass(), Func);
	}

	template<class ActorClass = AActor>
	static void ForEachActorWithLoading(UWorldPartition* WorldPartition, TFunctionRef<bool(AActor*)> Func)
	{
		ForEachActorWithLoading(WorldPartition, ActorClass::StaticClass(), Func);
	}

	static void ForEachIntersectingActorDesc(UWorldPartition* WorldPartition, const FBox& Box, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func);
	static void ForEachActorDesc(UWorldPartition* WorldPartition, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func);
	static void ForEachActorWithLoading(UWorldPartition* WorldPartition, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(AActor*)> Func);

	static bool HasExceededMaxMemory();
};

#endif // WITH_EDITOR
