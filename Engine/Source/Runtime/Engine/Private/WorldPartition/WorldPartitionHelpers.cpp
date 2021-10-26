// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionHelpers.h"

#include "Engine/Engine.h"
#include "RenderingThread.h"
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

namespace WorldPartitionHelpers
{
	void LoadReferences(UWorldPartition* WorldPartition, const FGuid& ActorGuid, TMap<FGuid, FWorldPartitionReference>& InOutActorReferences)
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
				LoadReferences(WorldPartition, ReferenceGuid, InOutActorReferences);
			}

			InOutActorReferences[ActorGuid] = FWorldPartitionReference(WorldPartition, ActorGuid);
		}
	}

	bool ForEachActorWithLoadingBody(const FGuid& ActorGuid, UWorldPartition* WorldPartition, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Predicate, bool bGCPerActor, TMap<FGuid, FWorldPartitionReference>& ActorReferences)
	{
		WorldPartitionHelpers::LoadReferences(WorldPartition, ActorGuid, ActorReferences);

		FWorldPartitionReference ActorReference(WorldPartition, ActorGuid);
		if (!Predicate(ActorReference.Get()))
		{
			return false;
		}

		if (bGCPerActor || FWorldPartitionHelpers::HasExceededMaxMemory())
		{
			ActorReferences.Empty();
			FWorldPartitionHelpers::DoCollectGarbage();
		}

		return true;
	}
}

void FWorldPartitionHelpers::ForEachActorWithLoading(UWorldPartition* WorldPartition, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Predicate, bool bGCPerActor)
{
	TMap<FGuid, FWorldPartitionReference> ActorReferences;
		
	ForEachActorDesc(WorldPartition, ActorClass, [&](const FWorldPartitionActorDesc* ActorDesc)
	{
		return WorldPartitionHelpers::ForEachActorWithLoadingBody(ActorDesc->GetGuid(), WorldPartition, Predicate, bGCPerActor, ActorReferences);
	});

	ActorReferences.Empty();
	DoCollectGarbage();
}

void FWorldPartitionHelpers::ForEachActorWithLoading(UWorldPartition* WorldPartition, const TArray<FGuid>& ActorGuids, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Predicate, bool bGCPerActor)
{
	TMap<FGuid, FWorldPartitionReference> ActorReferences;

	for(const FGuid& ActorGuid : ActorGuids)
	{
		if (!WorldPartitionHelpers::ForEachActorWithLoadingBody(ActorGuid, WorldPartition, Predicate, bGCPerActor, ActorReferences))
		{
			break;
		}
	}

	ActorReferences.Empty();
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

void FWorldPartitionHelpers::FakeEngineTick(UWorld* InWorld)
{
	check(InWorld);

	// Simulate an engine frame tick
	// Will make sure systems can perform their internal bookkeeping properly. For example, the VT system needs to 
	// process deleted VTs.

	if (FSceneInterface* Scene = InWorld->Scene)
	{
		// BeingFrame/EndFrame (taken from FEngineLoop)

		ENQUEUE_RENDER_COMMAND(BeginFrame)([](FRHICommandListImmediate& RHICmdList)
			{
				GFrameNumberRenderThread++;
				RHICmdList.BeginFrame();
				FCoreDelegates::OnBeginFrameRT.Broadcast();
			});

		ENQUEUE_RENDER_COMMAND(EndFrame)([](FRHICommandListImmediate& RHICmdList)
			{
				FCoreDelegates::OnEndFrameRT.Broadcast();
				RHICmdList.EndFrame();
			});

		FlushRenderingCommands();
	}
}

#endif // #if WITH_EDITOR
