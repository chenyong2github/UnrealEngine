// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionBuilder.h"

#include "CoreMinimal.h"

#include "DistanceFieldAtlas.h"
#include "MeshCardRepresentation.h"
#include "StaticMeshCompiler.h"


DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionBuilder, Log, All);


UWorldPartitionBuilder::UWorldPartitionBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}

bool UWorldPartitionBuilder::HasExceededMaxMemory() const
{
	const uint64 MemoryMinFreePhysical = 1024ll * 1024 * 1024;
	const uint64 MemoryMaxUsedPhysical = 16384ll * 1024 * 1024l;
	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
	return (MemStats.AvailablePhysical < MemoryMinFreePhysical) || (MemStats.UsedPhysical >= MemoryMaxUsedPhysical);
};

void UWorldPartitionBuilder::DoCollectGarbage() const
{
	if (GDistanceFieldAsyncQueue)
	{
		GDistanceFieldAsyncQueue->BlockUntilAllBuildsComplete();
	}

	if (GCardRepresentationAsyncQueue)
	{
		GCardRepresentationAsyncQueue->BlockUntilAllBuildsComplete();
	}

	const FPlatformMemoryStats MemStatsBefore = FPlatformMemory::GetStats();
	CollectGarbage(RF_NoFlags, true);
	const FPlatformMemoryStats MemStatsAfter = FPlatformMemory::GetStats();

	UE_LOG(LogWorldPartitionBuilder, Warning, TEXT("AvailablePhysical:%.2fGB AvailableVirtual %.2fGB"),
		((int64)MemStatsAfter.AvailablePhysical - (int64)MemStatsBefore.AvailablePhysical) / (1024.0 * 1024.0 * 1024.0),
		((int64)MemStatsAfter.AvailableVirtual - (int64)MemStatsBefore.AvailableVirtual) / (1024.0 * 1024.0 * 1024.0)
	);
};
