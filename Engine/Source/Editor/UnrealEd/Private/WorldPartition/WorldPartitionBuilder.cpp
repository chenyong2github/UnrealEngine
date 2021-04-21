// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionBuilder.h"

#include "CoreMinimal.h"

#include "DistanceFieldAtlas.h"
#include "MeshCardRepresentation.h"
#include "StaticMeshCompiler.h"
#include "Engine/World.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "Math/IntVector.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionBuilder, Log, All);


UWorldPartitionBuilder::UWorldPartitionBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}

bool UWorldPartitionBuilder::Run(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{
	UWorldPartition* WorldPartition = World->GetWorldPartition();

	// Properly Setup DataLayers for Builder
	if (const AWorldDataLayers* WorldDataLayers = AWorldDataLayers::Get(World))
	{
		// Load Data Layers
		bool bUpdateEditorCells = false;
		WorldDataLayers->ForEachDataLayer([&bUpdateEditorCells, this](UDataLayer* DataLayer)
		{
			// Load all Non DynamicallyLoaded Data Layers + Initially Active Data Layers + Data Layers provided by builder
			const bool bLoadedInEditor = (bLoadNonDynamicDataLayers && !DataLayer->IsDynamicallyLoaded()) || 
										 (bLoadInitiallyActiveDataLayers && DataLayer->GetInitialState() == EDataLayerState::Activated) ||
										 DataLayerLabels.Contains(DataLayer->GetDataLayerLabel());
			if (DataLayer->IsDynamicallyLoadedInEditor() != bLoadedInEditor)
			{
				bUpdateEditorCells = true;
				DataLayer->SetIsDynamicallyLoadedInEditor(bLoadedInEditor);
				if (RequiresCommandletRendering() && bLoadedInEditor)
				{
					DataLayer->SetIsInitiallyVisible(true);
				}
			}
			
			UE_LOG(LogWorldPartitionBuilder, Display, TEXT("DataLayer '%s' Loaded: %d"), *UDataLayer::GetDataLayerText(DataLayer).ToString(), bLoadedInEditor ? 1 : 0);
			
			return true;
		});

		if (bUpdateEditorCells)
		{
			UE_LOG(LogWorldPartitionBuilder, Display, TEXT("DataLayer load state changed refreshing editor cells"));
			WorldPartition->RefreshLoadedEditorCells();
		}
	}
		
	ELoadingMode LoadingMode = GetLoadingMode();
	if (LoadingMode == ELoadingMode::IterativeCells)
	{
		// do partial loading loop that calls RunInternal
		FBox EditorBounds = WorldPartition->GetEditorWorldBounds();

		auto GetCellCoord = [](FVector InPos, int32 InCellSize)
		{
			return FIntVector(
				FMath::FloorToInt(InPos.X / InCellSize),
				FMath::FloorToInt(InPos.Y / InCellSize),
				FMath::FloorToInt(InPos.Z / InCellSize)
			);
		};

		const FIntVector MinCellCoords = GetCellCoord(EditorBounds.Min, IterativeCellSize);
		const FIntVector MaxCellCoords = GetCellCoord(EditorBounds.Max, IterativeCellSize);

		UE_LOG(LogWorldPartitionBuilder, Display, TEXT("Iterative Cell Mode"));
		UE_LOG(LogWorldPartitionBuilder, Display, TEXT("Cell Size %d"), IterativeCellSize);
		UE_LOG(LogWorldPartitionBuilder, Display, TEXT("Cell Overlap %d"), IterativeCellOverlapSize);
		UE_LOG(LogWorldPartitionBuilder, Display, TEXT("WorldBounds: Min %s, Max %s"), *EditorBounds.Min.ToString(), *EditorBounds.Max.ToString());
		UE_LOG(LogWorldPartitionBuilder, Display, TEXT("Iteration Count: %d"), (MaxCellCoords.Z-MinCellCoords.Z) * (MaxCellCoords.Y-MinCellCoords.Y) * (MaxCellCoords.X - MinCellCoords.X));
		

		FBox LoadedBounds(ForceInit);
		for (int32 z = MinCellCoords.Z; z <= MaxCellCoords.Z; z++)
		{
			for (int32 y = MinCellCoords.Y; y <= MaxCellCoords.Y; y++)
			{
				for (int32 x = MinCellCoords.X; x <= MaxCellCoords.X; x++)
				{
					const FVector Min(x * IterativeCellSize, y * IterativeCellSize, z * IterativeCellSize);
					const FVector Max = Min + FVector(IterativeCellSize);
					FBox BoundsToLoad(Min, Max);
					BoundsToLoad.ExpandBy(IterativeCellOverlapSize);

					UE_LOG(LogWorldPartitionBuilder, Verbose, TEXT("Loading Bounds: Min %s, Max %s"), *BoundsToLoad.Min.ToString(), *BoundsToLoad.Max.ToString());
					WorldPartition->LoadEditorCells(BoundsToLoad);
					LoadedBounds += BoundsToLoad;


					if (!RunInternal(World, BoundsToLoad, PackageHelper))
					{
						return false;
					}

					if (HasExceededMaxMemory())
					{
						WorldPartition->UnloadEditorCells(LoadedBounds);
						// Reset Loaded Bounds
						LoadedBounds.Init();
						DoCollectGarbage();
					}
				}
			}
		}

		return true;
	}
	else
	{
		FBox BoundsToLoad(ForceInit);
		if (LoadingMode == ELoadingMode::EntireWorld)
		{
			BoundsToLoad += FBox(FVector(-WORLD_MAX, -WORLD_MAX, -WORLD_MAX), FVector(WORLD_MAX, WORLD_MAX, WORLD_MAX));
			WorldPartition->LoadEditorCells(BoundsToLoad);
		}

		return RunInternal(World, BoundsToLoad, PackageHelper);
	}
}

bool UWorldPartitionBuilder::HasExceededMaxMemory() const
{
	const uint64 MemoryMinFreePhysical = 1024ll * 1024 * 1024;
	const uint64 MemoryMaxUsedPhysical = 32768ll * 1024 * 1024l;
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
