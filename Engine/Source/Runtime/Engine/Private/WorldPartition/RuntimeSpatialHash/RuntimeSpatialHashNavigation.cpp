// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"

#if WITH_EDITOR

#include "WorldPartition/RuntimeSpatialHash/RuntimeSpatialHashGridHelper.h"
#include "WorldPartition/NavigationData/NavigationDataChunkActor.h"

#include "EngineUtils.h"

#include "AI/NavigationSystemBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionRuntimeSpatialHashNav, Log, All);

bool UWorldPartitionRuntimeSpatialHash::GenerateNavigationData()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionRuntimeSpatialHash::GenerateNavigationData);

	UE_LOG(LogWorldPartitionRuntimeSpatialHashNav, Log, TEXT("%s"), ANSI_TO_TCHAR(__FUNCTION__));

	UWorldPartition* WorldPartition = GetOuterUWorldPartition();
	UWorld* World = WorldPartition->World;

	// Generate navmesh
	// Make sure navigation is added and initialized in EditorMode
	FNavigationSystem::AddNavigationSystemToWorld(*World, FNavigationSystemRunMode::EditorMode);

	// Invoke navigation data generator
	FNavigationSystem::Build(*World);

	// For each cell, gather navmesh and generate a datachunk actor
	const FBox WorldBounds = WorldPartition->GetWorldBounds();

	const int32 GridIndex = 0; //Todo At: only work with grid 0 for now.
	const uint32 GridLevel = 3; //Todo AT, only generate for level 3 for now
	UE_LOG(LogWorldPartitionRuntimeSpatialHashNav, Verbose, TEXT("Generate NavDataChunk actor for GridIndex %i."), GridIndex);

	const FSpatialHashRuntimeGrid& RuntimeGrid = Grids[GridIndex];
	const FSquare2DGridHelper GridHelper = GetGridHelper(WorldBounds, RuntimeGrid.CellSize);
	int32 ActorCount = 0;

	const UNavigationSystemBase* NavSystem = World->GetNavigationSystem();
	if (NavSystem == nullptr)
	{
		UE_LOG(LogWorldPartitionRuntimeSpatialHashNav, Verbose, TEXT("No navigation system to generate navigation data."));
		return false;
	}

	// Keep track of all valid navigation data chunk actors
	TSet<ANavigationDataChunkActor*> ValidNavigationDataChunkActors;

	const FSquare2DGridHelper::FGridLevel& GridLevelHelper = GridHelper.Levels[GridLevel];

	GridLevelHelper.ForEachCells([GridLevel, &GridHelper, &GridLevelHelper, RuntimeGrid, WorldPartition, &ActorCount, World, NavSystem, &ValidNavigationDataChunkActors, this](const FIntVector2& CellCoord)
		{
			FBox2D CellBounds;
			GridLevelHelper.GetCellBounds(CellCoord, CellBounds);
			if (CellBounds.GetExtent().X < 1.f || CellBounds.GetExtent().Y < 1.f)
			{
				// Safety, since we reduce by 1.f below.
				UE_LOG(LogWorldPartitionRuntimeSpatialHashNav, Warning, TEXT("%s: grid cell too small."), ANSI_TO_TCHAR(__FUNCTION__));
				return;
			}

			const float HalfHeight = HALF_WORLD_MAX;
			const FBox QueryBounds(FVector(CellBounds.Min.X, CellBounds.Min.Y, -HalfHeight), FVector(CellBounds.Max.X, CellBounds.Max.Y, HalfHeight));

			if (NavSystem->ContainsNavData(QueryBounds) == false)
			{
				// Skip if there is no navdata for this cell
				return;
			}

			const FSquare2DGridHelper::FGridLevel::FGridCell& GridCell = GridLevelHelper.GetCell(CellCoord);

			//@todo_ow: Properly handle data layers
			if (const FSquare2DGridHelper::FGridLevel::FGridCellDataChunk* GridCellDataChunk = GridCell.GetNoDataLayersDataChunk())
			{
				FActorSpawnParameters SpawnParams;
				SpawnParams.bDeferConstruction = true;
				SpawnParams.bCreateActorPackage = true;
				ANavigationDataChunkActor* DataChunkActor = World->SpawnActor<ANavigationDataChunkActor>(SpawnParams);
				ActorCount++;

				const FVector2D CellCenter = CellBounds.GetCenter();
				DataChunkActor->SetActorLocation(FVector(CellCenter.X, CellCenter.Y, 0.f));

				FBox TilesBounds(EForceInit::ForceInit);
				DataChunkActor->CollectNavData(QueryBounds, TilesBounds);

				FBox ChunkActorBounds(FVector(QueryBounds.Min.X, QueryBounds.Min.Y, TilesBounds.Min.Z), FVector(QueryBounds.Max.X, QueryBounds.Max.Y, TilesBounds.Max.Z));
				ChunkActorBounds = ChunkActorBounds.ExpandBy(FVector(-1.f, -1.f, 1.f)); //reduce XY by 1cm to avoid precision issues causing potential overflow on neighboring cell, add 1cm in Z to have a minimum of volume.
				UE_LOG(LogWorldPartitionRuntimeSpatialHashNav, VeryVerbose, TEXT("Setting ChunkActorBounds to %s"), *ChunkActorBounds.ToString());
				DataChunkActor->SetDataChunkActorBounds(ChunkActorBounds);

				FIntVector CellGlobalCoord;
				verify(GridHelper.GetCellGlobalCoords(FIntVector(CellCoord.X, CellCoord.Y, GridLevel), CellGlobalCoord));
				const FName CellName = GetCellName(RuntimeGrid.GridName, CellGlobalCoord, GridCellDataChunk->GetDataLayersID());
				DataChunkActor->SetActorLabel(FString::Printf(TEXT("NavDataChunkActor_%s_%s"), *GetName(), *CellName.ToString()));

				// Set target grid
				DataChunkActor->SetRuntimeGrid(RuntimeGrid.GridName);
				ValidNavigationDataChunkActors.Add(DataChunkActor);

				UE_LOG(LogWorldPartitionRuntimeSpatialHashNav, Verbose, TEXT("%i) %s added."), ActorCount, *DataChunkActor->GetName());
			}
		});

	// Destroy all invalid navigation data chunk actors
	for (TActorIterator<ANavigationDataChunkActor> It(GetWorld()); It; ++It)
	{
		if (!ValidNavigationDataChunkActors.Contains(*It))
		{
			GetWorld()->DestroyActor(*It);
		}
	}

	return true;
}

#endif // #if WITH_EDITOR