// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionHelpers.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionEditorHash.h"

#if WITH_EDITOR

void FWorldPartitionHelpers::ForEachIntersectingActorDesc(UWorldPartition* WorldPartition, const FBox& Box, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Predicate)
{
	WorldPartition->EditorHash->ForEachIntersectingActor(Box, [&ActorClass, Predicate](const FWorldPartitionActorDesc* ActorDesc)
	{
		if (ActorDesc->GetActorClass()->IsChildOf(ActorClass))
		{
			Predicate(ActorDesc);
		}
	});
}

void FWorldPartitionHelpers::ForEachActorDesc(UWorldPartition* WorldPartition, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Predicate)
{
	for (UActorDescContainer::TConstIterator<> ActorDescIterator(WorldPartition, ActorClass); ActorDescIterator; ++ActorDescIterator)
	{
		if (!Predicate(*ActorDescIterator))
		{
			return;
		}
	}
}

void FWorldPartitionHelpers::ForEachActorWithLoading(UWorldPartition* WorldPartition, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(AActor*)> Predicate)
{
	// Recursive loading of references 
	TFunction<void(const FGuid&, TMap<FGuid, FWorldPartitionReference>&)> LoadReferences = [WorldPartition, &LoadReferences](const FGuid& ActorGuid, TMap<FGuid, FWorldPartitionReference>& InOutActorReferences)
	{
		if (InOutActorReferences.Contains(ActorGuid))
		{
			return;
		}

		if (const FWorldPartitionActorDesc* ActorDesc = WorldPartition->GetActorDesc(ActorGuid))
		{
			for (FGuid ReferenceGuid : ActorDesc->GetReferences())
			{
				LoadReferences(ReferenceGuid, InOutActorReferences);
			}

			InOutActorReferences.Add(ActorGuid, FWorldPartitionReference(WorldPartition, ActorGuid));
		}
	};

	TMap<FGuid, FWorldPartitionReference> ActorReferences;
	TArray<FGuid> ActorsToProcess;

	ForEachActorDesc(WorldPartition, ActorClass, [&](const FWorldPartitionActorDesc* ActorDesc)
	{
		const FGuid& ActorGuid = ActorDesc->GetGuid();
		LoadReferences(ActorGuid, ActorReferences);

		FWorldPartitionReference ActorReference(WorldPartition, ActorGuid);
		AActor* Actor = ActorReference.Get()->GetActor();

		if (!Predicate(Actor))
		{
			return false;
		}

		if (HasExceededMaxMemory())
		{
			ActorReferences.Empty();

			const FPlatformMemoryStats MemStatsBefore = FPlatformMemory::GetStats();
			CollectGarbage(RF_NoFlags, true);
			const FPlatformMemoryStats MemStatsAfter = FPlatformMemory::GetStats();

			UE_LOG(LogWorldPartition, Log, TEXT("GC Performed - Available Physical: %.2fGB, Available Virtual: %.2fGB"),
				(int64)MemStatsAfter.AvailablePhysical / (1024.0 * 1024.0 * 1024.0),
				(int64)MemStatsAfter.AvailableVirtual / (1024.0 * 1024.0 * 1024.0)
			);
		}

		return true;
	});
}

bool FWorldPartitionHelpers::HasExceededMaxMemory()
{
	const uint64 MemoryMinFreePhysical = 1 * 1024ll * 1024ll * 1024ll;
	const uint64 MemoryMaxUsedPhysical = 32 * 1024ll * 1024ll * 1024ll;
	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();

	const bool bHasExceededMinFreePhysical = MemStats.AvailablePhysical < MemoryMinFreePhysical;
	const bool bHasExceededMaxUsedPhysical = MemStats.UsedPhysical >= MemoryMaxUsedPhysical;
	const bool bHasExceededMaxMemory = bHasExceededMinFreePhysical || bHasExceededMaxUsedPhysical;

	return bHasExceededMaxMemory;
};

#endif // #if WITH_EDITOR
