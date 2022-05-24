// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionHelpers.h"

#include "Engine/Engine.h"
#include "RenderingThread.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionEditorHash.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionHelpers, Log, All);

#if WITH_EDITOR

bool FWorldPartitionHelpers::IsActorDescClassCompatibleWith(const FWorldPartitionActorDesc* ActorDesc, const UClass* Class)
{
	check(Class);

	UClass* ActorNativeClass = ActorDesc->GetActorNativeClass();
	UClass* ActorBaseClass = ActorNativeClass;

	if (!Class->IsNative())
	{
		if (FName ActorBaseClassName = ActorDesc->GetBaseClass(); !ActorBaseClassName.IsNone())
		{
			ActorBaseClass = LoadClass<AActor>(nullptr, *ActorBaseClassName.ToString(), nullptr, LOAD_None, nullptr);

			if (!ActorBaseClass)
			{
				UE_LOG(LogWorldPartitionHelpers, Warning, TEXT("Failed to find actor base class: %s."), *ActorBaseClassName.ToString());
				ActorBaseClass = ActorNativeClass;
			}
		}
	}

	return ActorBaseClass->IsChildOf(Class);
}

void FWorldPartitionHelpers::ForEachIntersectingActorDesc(UWorldPartition* WorldPartition, const FBox& Box, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func)
{
	WorldPartition->EditorHash->ForEachIntersectingActor(Box, [&ActorClass, Func](const FWorldPartitionActorDesc* ActorDesc)
	{
		if (IsActorDescClassCompatibleWith(ActorDesc, ActorClass))
		{
			Func(ActorDesc);
		}
	});
}

void FWorldPartitionHelpers::ForEachActorDesc(UWorldPartition* WorldPartition, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func)
{
	for (UActorDescContainer::TConstIterator<> ActorDescIterator(WorldPartition); ActorDescIterator; ++ActorDescIterator)
	{
		if (IsActorDescClassCompatibleWith(*ActorDescIterator, ActorClass))
		{
			if (!Func(*ActorDescIterator))
			{
				return;
			}
		}
	}
}

namespace WorldPartitionHelpers
{
	void LoadReferences(UWorldPartition* WorldPartition, const FGuid& ActorGuid, TMap<FGuid, FWorldPartitionReference>& InOutActorReferences)
	{
		FWorldPartitionLoadingContext::FDeferred LoadingContext;

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
}

FWorldPartitionHelpers::FForEachActorWithLoadingParams::FForEachActorWithLoadingParams()
	: bGCPerActor(false)
	, ActorClass(AActor::StaticClass())
{}

void FWorldPartitionHelpers::ForEachActorWithLoading(UWorldPartition* WorldPartition, TSubclassOf<AActor> ActorClass, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func, TFunctionRef<void()> OnReleasingActorReferences, bool bGCPerActor)
{
	FForEachActorWithLoadingParams Params;
	Params.ActorClass = ActorClass;
	Params.OnPreGarbageCollect = [&OnReleasingActorReferences]() { OnReleasingActorReferences(); };
	ForEachActorWithLoading(WorldPartition, Func, Params);
}

void FWorldPartitionHelpers::ForEachActorWithLoading(UWorldPartition* WorldPartition, const TArray<FGuid>& ActorGuids, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func, TFunctionRef<void()> OnReleasingActorReferences, bool bGCPerActor)
{
	TSet<FGuid> ActorGuidsSet(ActorGuids);
	FForEachActorWithLoadingParams Params;
	Params.FilterActorDesc = [&ActorGuidsSet](const FWorldPartitionActorDesc* ActorDesc) -> bool { return ActorGuidsSet.Contains(ActorDesc->GetGuid());	};
	Params.OnPreGarbageCollect = [&OnReleasingActorReferences]() { OnReleasingActorReferences(); };	
	ForEachActorWithLoading(WorldPartition, Func, Params);
}

void FWorldPartitionHelpers::ForEachActorWithLoading(UWorldPartition* WorldPartition, TFunctionRef<bool(const FWorldPartitionActorDesc*)> Func, const FForEachActorWithLoadingParams& Params)
{
	TMap<FGuid, FWorldPartitionReference> ActorReferences;

	auto CallGarbageCollect = [&Params, &ActorReferences]()
	{
		if (Params.OnPreGarbageCollect)
		{
			Params.OnPreGarbageCollect();
		}

		ActorReferences.Empty();
		DoCollectGarbage();
	};

	for (UActorDescContainer::TConstIterator<> ActorDescIterator(WorldPartition); ActorDescIterator; ++ActorDescIterator)
	{
		if (IsActorDescClassCompatibleWith(*ActorDescIterator, Params.ActorClass))
		{
			if (!Params.FilterActorDesc || Params.FilterActorDesc(*ActorDescIterator))
			{
				WorldPartitionHelpers::LoadReferences(WorldPartition, ActorDescIterator->GetGuid(), ActorReferences);

				FWorldPartitionReference ActorReference(WorldPartition, ActorDescIterator->GetGuid());
				if (!Func(ActorReference.Get()))
				{
					break;
				}

				if (Params.bGCPerActor || FWorldPartitionHelpers::HasExceededMaxMemory())
				{
					CallGarbageCollect();
				}
			}
		}
	}

	CallGarbageCollect();
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
