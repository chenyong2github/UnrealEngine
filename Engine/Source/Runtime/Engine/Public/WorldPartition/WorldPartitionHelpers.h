// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "WorldPartition/WorldPartitionRuntimeCellInterface.h"

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionHandle.h"
#endif

class UWorld;
class UWorldPartition;

#if WITH_EDITOR
class FWorldPartitionActorDesc;
#endif

namespace FWorldPartitionHelpersPrivate
{
	ENGINE_API UWorldPartition* GetWorldPartitionFromObject(const UObject* InObject);

	template <class T>
	inline UWorldPartition* GetWorldPartition(const T* InObject)
	{
		return IsValid(InObject) ? GetWorldPartitionFromObject(InObject) : nullptr;
	}

	template <>
	inline UWorldPartition* GetWorldPartition(const ULevel* InLevel)
	{
		if (IsValid(InLevel))
		{
			if (const IWorldPartitionCell* Cell = InLevel->GetWorldPartitionRuntimeCell())
			{
				return Cell->GetOuterWorld()->GetWorldPartition();
			}		
			else if (const UWorld* OuterWorld = Cast<UWorld>(InLevel->GetOuter()))
			{
				return OuterWorld->GetWorldPartition();
			}
		}
			
		return nullptr;
	}

	template <>
	inline UWorldPartition* GetWorldPartition(const UWorld* InWorld)
	{
		return IsValid(InWorld) ? GetWorldPartition<ULevel>(InWorld->PersistentLevel) : nullptr;
	}

	template <>
	inline UWorldPartition* GetWorldPartition(const AActor* InActor)
	{
		return IsValid(InActor) ? GetWorldPartition<ULevel>(InActor->GetLevel()) : nullptr;
	}
}

class ENGINE_API FWorldPartitionHelpers
{
public:
	/** Returns the owning World Partition for this object. */
	template <class T>
	static inline UWorldPartition* GetWorldPartition(const T* InObject)
	{
		return FWorldPartitionHelpersPrivate::GetWorldPartition(InObject);
	}

#if WITH_EDITOR
private:
	static bool IsActorDescClassCompatibleWith(const FWorldPartitionActorDesc* ActorDesc, const UClass* Class);

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
	struct ENGINE_API FForEachActorWithLoadingParams
	{
		FForEachActorWithLoadingParams();

		/* Perform a garbage collection per-actor, useful to test if the caller properly handle GCs. */
		bool bGCPerActor;

		/* Prevent clearing of actor references. */
		bool bKeepReferences;

		/* The classes used to filter actors loading. */
		TArray<TSubclassOf<AActor>> ActorClasses;

		/* If not empty, iteration will be done only on those actors. */
		TArray<FGuid> ActorGuids;

		/* Custom filter function used to filter actors loading. */
		TUniqueFunction<bool(const FWorldPartitionActorDesc*)> FilterActorDesc;

		/* Called right before releasing actor references and performing garbage collection. */
		TUniqueFunction<void()> OnPreGarbageCollect;
	};

	/* Struct of optional output from foreach actordesc functions. */
	struct ENGINE_API FForEachActorWithLoadingResult
	{
		/* Reference to all actors and actor references loaded by the foreach actordesc function */
		TMap<FGuid, FWorldPartitionReference> ActorReferences;
	};

	UE_DEPRECATED(5.1, "ForEachActorWithLoading is deprecated, ForEachActorWithLoading with FForEachActorWithLoadingParams should be used instead.")
	static void ForEachActorWithLoading(UWorldPartition* WorldPartition, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func, TFunctionRef<void()> OnReleasingActorReferences = [](){}, bool bGCPerActor = false);

	UE_DEPRECATED(5.1, "ForEachActorWithLoading is deprecated, ForEachActorWithLoading with FForEachActorWithLoadingParams should be used instead.")
	static void ForEachActorWithLoading(UWorldPartition* WorldPartition, const TArray<FGuid>& ActorGuids, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func, TFunctionRef<void()> OnReleasingActorReferences = [](){}, bool bGCPerActor = false);

	static void ForEachActorWithLoading(UWorldPartition* WorldPartition, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func, const FForEachActorWithLoadingParams& Params = FForEachActorWithLoadingParams());
	static void ForEachActorWithLoading(UWorldPartition* WorldPartition, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func, const FForEachActorWithLoadingParams& Params, FForEachActorWithLoadingResult& Result);
	
	static bool HasExceededMaxMemory();
	static bool ShouldCollectGarbage();
	static void DoCollectGarbage();

	// Simulate an engine frame tick
	static void FakeEngineTick(UWorld* World);

	// Editor/Runtime conversions
	static bool ConvertRuntimePathToEditorPath(const FSoftObjectPath& InPath, FSoftObjectPath& OutPath);
	static bool ConvertEditorPathToRuntimePath(const FSoftObjectPath& InPath, FSoftObjectPath& OutPath);
#endif // WITH_EDITOR
};


