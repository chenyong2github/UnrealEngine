// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionHelpers.h"

#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionEditorHash.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionHelpers, Log, All);


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

void FWorldPartitionHelpers::ForEachActorWithLoading(UWorldPartition* WorldPartition, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Predicate)
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
			InOutActorReferences.Emplace(ActorGuid);
			
			for (FGuid ReferenceGuid : ActorDesc->GetReferences())
			{
				LoadReferences(ReferenceGuid, InOutActorReferences);
			}

			InOutActorReferences[ActorGuid] = FWorldPartitionReference(WorldPartition, ActorGuid);
		}
	};

	TMap<FGuid, FWorldPartitionReference> ActorReferences;
	TArray<FGuid> ActorsToProcess;

	auto DoCollectGarbage = [&ActorReferences]()
	{
		ActorReferences.Empty();

		const FPlatformMemoryStats MemStatsBefore = FPlatformMemory::GetStats();
		CollectGarbage(RF_NoFlags, true);
		const FPlatformMemoryStats MemStatsAfter = FPlatformMemory::GetStats();

		UE_LOG(LogWorldPartition, Log, TEXT("GC Performed - Available Physical: %.2fGB, Available Virtual: %.2fGB"),
			(int64)MemStatsAfter.AvailablePhysical / (1024.0 * 1024.0 * 1024.0),
			(int64)MemStatsAfter.AvailableVirtual / (1024.0 * 1024.0 * 1024.0)
		);
	};

	ForEachActorDesc(WorldPartition, ActorClass, [&](const FWorldPartitionActorDesc* ActorDesc)
	{
		const FGuid& ActorGuid = ActorDesc->GetGuid();
		LoadReferences(ActorGuid, ActorReferences);

		FWorldPartitionReference ActorReference(WorldPartition, ActorGuid);
		if (!Predicate(ActorReference.Get()))
		{
			return false;
		}

		if (HasExceededMaxMemory())
		{
			DoCollectGarbage();
		}

		return true;
	});

	DoCollectGarbage();
}

bool FWorldPartitionHelpers::HasExceededMaxMemory()
{
	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();

	const uint64 MemoryMinFreePhysical = 1llu * 1024 * 1024 * 1024;
	const uint64 MemoryMaxUsedPhysical = FMath::Max(32llu * 1024 * 1024 * 1024, MemStats.TotalPhysical / 2);

	const bool bHasExceededMinFreePhysical = MemStats.AvailablePhysical < MemoryMinFreePhysical;
	const bool bHasExceededMaxUsedPhysical = MemStats.UsedPhysical >= MemoryMaxUsedPhysical;
	const bool bHasExceededMaxMemory = bHasExceededMinFreePhysical || bHasExceededMaxUsedPhysical;

	// Even if we're not exhausting memory, GC should be run at periodic intervals
	return bHasExceededMaxMemory || (FPlatformTime::Seconds() - GetLastGCTime()) > 30;
};

void FWorldPartitionHelpers::DoCollectGarbage()
{
	const FPlatformMemoryStats MemStatsBefore = FPlatformMemory::GetStats();
	CollectGarbage(RF_NoFlags, true);
	const FPlatformMemoryStats MemStatsAfter = FPlatformMemory::GetStats();

	UE_LOG(LogWorldPartition, Log, TEXT("GC Performed - Available Physical: %.2fGB, Available Virtual: %.2fGB"),
		(int64)MemStatsAfter.AvailablePhysical / (1024.0 * 1024.0 * 1024.0),
		(int64)MemStatsAfter.AvailableVirtual / (1024.0 * 1024.0 * 1024.0)
	);
};

#endif // #if WITH_EDITOR
