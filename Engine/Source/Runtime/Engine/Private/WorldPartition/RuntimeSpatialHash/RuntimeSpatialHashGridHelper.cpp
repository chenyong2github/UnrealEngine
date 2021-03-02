// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/RuntimeSpatialHash/RuntimeSpatialHashGridHelper.h"

#include "ProfilingDebugging/ScopedTimers.h"

FSquare2DGridHelper::FSquare2DGridHelper(const FBox& InWorldBounds, const FVector& InOrigin, int32 InCellSize)
	: WorldBounds(InWorldBounds)
	, Origin(InOrigin)
	, CellSize(InCellSize)
{
	// Compute Grid's size and level count based on World bounds
	float WorldBoundsMaxExtent = 0.f;
	if (WorldBounds.IsValid)
	{
		const FVector2D DistMin = FMath::Abs(FVector2D(WorldBounds.Min - Origin));
		const FVector2D DistMax = FMath::Abs(FVector2D(WorldBounds.Max - Origin));
		WorldBoundsMaxExtent = FMath::Max(DistMin.GetMax(), DistMax.GetMax());
	}
	int32 GridSize = 1;
	int32 GridLevelCount = 1;
	if (WorldBoundsMaxExtent > 0.f)
	{
		GridSize = 2.f * FMath::CeilToFloat(WorldBoundsMaxExtent / CellSize); 
		if (!FMath::IsPowerOfTwo(GridSize))
		{
			GridSize = FMath::Pow(2.f, FMath::CeilToFloat(FMath::Log2(static_cast<float>(GridSize))));
		}
		GridLevelCount = FMath::FloorLog2(GridSize) + 1;
	}
	else
	{
		UE_LOG(LogWorldPartitionRuntimeSpatialHash, Warning, TEXT("Invalid world bounds, grid partitioning will use a runtime grid with 1 cell."));
	}

	check(FMath::IsPowerOfTwo(GridSize));

	Levels.Reserve(GridLevelCount);
	int32 CurrentCellSize = CellSize;
	int32 CurrentGridSize = GridSize;
	for (int32 Level = 0; Level < GridLevelCount; ++Level)
	{
		// Except for top level, adding 1 to CurrentGridSize (which is always a power of 2) breaks the pattern of perfectly aligned cell edges between grid level cells.
		// This will prevent weird artefact during actor promotion when an actor is placed using its bounds and which overlaps multiple cells.
		// In this situation, the algorithm will try to find a cell that encapsulates completely the actor's bounds by searching in the upper levels, until it finds one.
		// Also note that, the default origin of each level will always be centered at the middle of the bounds of (level's cellsize * level's grid size).
		int32 LevelGridSize = (Level == GridLevelCount-1) ? CurrentGridSize : CurrentGridSize + 1;
		
		Levels.Emplace(FVector2D(InOrigin), CurrentCellSize, LevelGridSize, Level);

		CurrentCellSize <<= 1;
		CurrentGridSize >>= 1;
	}

#if WITH_EDITOR
	// Make sure the always loaded cell exists
	GetAlwaysLoadedCell();
#endif
}

void FSquare2DGridHelper::ForEachCells(TFunctionRef<void(const FSquare2DGridHelper::FGridLevel::FGridCell&)> InOperation) const
{
	for (int32 Level = 0; Level < Levels.Num(); Level++)
	{
		for (const FSquare2DGridHelper::FGridLevel::FGridCell& ThisCell : Levels[Level].Cells)
		{
			InOperation(ThisCell);
		}
	}
}

int32 FSquare2DGridHelper::ForEachIntersectingCells(const FBox& InBox, TFunctionRef<void(const FIntVector&)> InOperation) const
{
	int32 NumCells = 0;

	for (int32 Level = 0; Level < Levels.Num(); Level++)
	{
		NumCells += Levels[Level].ForEachIntersectingCells(InBox, [InBox, Level, InOperation](const FIntVector2& Coord) { InOperation(FIntVector(Coord.X, Coord.Y, Level)); });
	}

	return NumCells;
}

int32 FSquare2DGridHelper::ForEachIntersectingCells(const FSphere& InSphere, TFunctionRef<void(const FIntVector&)> InOperation) const
{
	int32 NumCells = 0;

	for (int32 Level = 0; Level < Levels.Num(); Level++)
	{
		NumCells += Levels[Level].ForEachIntersectingCells(InSphere, [InSphere, Level, InOperation](const FIntVector2& Coord) { InOperation(FIntVector(Coord.X, Coord.Y, Level)); });
	}

	return NumCells;
}

#if WITH_EDITOR
void FSquare2DGridHelper::ValidateSingleActorReferer()
{
	UE_SCOPED_TIMER(TEXT("ValidateSingleActorReferer"), LogWorldPartitionRuntimeSpatialHash);

	TSet<FActorInstance> ActorUsage;
	for (int32 Level = 0; Level < Levels.Num() - 1; Level++)
	{
		for (const FSquare2DGridHelper::FGridLevel::FGridCell& ThisCell : Levels[Level].Cells)
		{
			for (const FGridLevel::FGridCellDataChunk& DataChunk : ThisCell.GetDataChunks())
			{
				for (const FActorInstance& ActorInstance : DataChunk.GetActors())
				{
					bool bWasAlreadyInSet;
					ActorUsage.Add(ActorInstance, &bWasAlreadyInSet);
					check(!bWasAlreadyInSet);
				}
			}
		}
	}
}
#endif // #if WITH_EDITOR

#if WITH_EDITOR

FSquare2DGridHelper GetGridHelper(const FBox& WorldBounds, int32 GridCellSize)
{
	// Default grid to a minimum of 1 level and 1 cell, for always loaded actors
	FVector GridOrigin = FVector::ZeroVector;
	return FSquare2DGridHelper(WorldBounds, GridOrigin, GridCellSize);
}

FSquare2DGridHelper GetPartitionedActors(const UWorldPartition* WorldPartition, const FBox& WorldBounds, const FSpatialHashRuntimeGrid& Grid, const TArray<const FActorClusterInstance*>& GridActors)
{
	UE_SCOPED_TIMER(TEXT("GetPartitionedActors"), LogWorldPartitionRuntimeSpatialHash);

	//
	// Create the hierarchical grids for the game
	//	
	FSquare2DGridHelper PartitionedActors = GetGridHelper(WorldBounds, Grid.CellSize);
	if (ensure(PartitionedActors.Levels.Num()) && WorldBounds.IsValid)
	{
		int32 IntersectingCellCount = 0;
		FSquare2DGridHelper::FGridLevel& LastGridLevel = PartitionedActors.Levels.Last();
		LastGridLevel.ForEachIntersectingCells(WorldBounds, [&IntersectingCellCount](const FIntVector2& Coords) { ++IntersectingCellCount; });
		if (!ensure(IntersectingCellCount == 1))
		{
			UE_LOG(LogWorldPartitionRuntimeSpatialHash, Warning, TEXT("Can't find grid cell that encompasses world bounds."));
		}
	}

	for (const FActorClusterInstance* ClusterInstance : GridActors)
	{
		const FActorCluster* ActorCluster = ClusterInstance->Cluster;
		check(ActorCluster && ActorCluster->Actors.Num() > 0);
		EActorGridPlacement GridPlacement = ActorCluster->GridPlacement;
		bool bAlwaysLoadedPromotedCluster = (GridPlacement == EActorGridPlacement::None);
		bool bAlwaysLoadedPromotedOutOfGrid = false;

		if (bAlwaysLoadedPromotedCluster)
		{
			GridPlacement = EActorGridPlacement::AlwaysLoaded;
		}

		switch (GridPlacement)
		{
		case EActorGridPlacement::Location:
		{
			FIntVector2 CellCoords;
			check(ActorCluster->Actors.Num() == 1);
			const FGuid& ActorGuid = *ActorCluster->Actors.CreateConstIterator();
			FActorInstance ActorInstance(ActorGuid, ClusterInstance->ContainerInstance);
			if (ensure(PartitionedActors.GetLowestLevel().GetCellCoords(FVector2D(ActorInstance.GetActorDescView().GetOrigin()), CellCoords)))
			{
				PartitionedActors.GetLowestLevel().GetCell(CellCoords).AddActor(MoveTemp(ActorInstance), ClusterInstance->DataLayers);
			}
			else
			{
				GridPlacement = EActorGridPlacement::AlwaysLoaded;
				bAlwaysLoadedPromotedOutOfGrid = true;
				PartitionedActors.GetAlwaysLoadedCell().AddActor(MoveTemp(ActorInstance), ClusterInstance->DataLayers);
			}
			break;
		}
		case EActorGridPlacement::Bounds:
		{
			// Find grid level cell that encompasses the actor cluster and put actors in it.
			bool bFoundCell = false;
			for (FSquare2DGridHelper::FGridLevel& GridLevel : PartitionedActors.Levels)
			{
				int32 IntersectingCellCount = 0;
				GridLevel.ForEachIntersectingCellsBreakable(ClusterInstance->Bounds, [&IntersectingCellCount](const FIntVector2& Coords) { return ++IntersectingCellCount <= 1; });
				if (IntersectingCellCount == 1)
				{
					GridLevel.ForEachIntersectingCells(ClusterInstance->Bounds, [&GridLevel, &ActorCluster, &ClusterInstance](const FIntVector2& Coords)
						{
							GridLevel.GetCell(Coords).AddActors(ActorCluster->Actors, ClusterInstance->ContainerInstance, ClusterInstance->DataLayers);
						});
					bFoundCell = true;
					break;
				}
			}
			if (!(ensure(bFoundCell)))
			{
				GridPlacement = EActorGridPlacement::AlwaysLoaded;
				bAlwaysLoadedPromotedOutOfGrid = true;
				PartitionedActors.GetAlwaysLoadedCell().AddActors(ActorCluster->Actors, ClusterInstance->ContainerInstance, ClusterInstance->DataLayers);
			}
			break;
		}
		case EActorGridPlacement::AlwaysLoaded:
		{
			PartitionedActors.GetAlwaysLoadedCell().AddActors(ActorCluster->Actors, ClusterInstance->ContainerInstance, ClusterInstance->DataLayers);
			break;
		}
		default:
			check(0);
		}

		if (UE_LOG_ACTIVE(LogWorldPartitionRuntimeSpatialHash, Verbose))
		{
			if (ActorCluster->Actors.Num() > 1)
			{
				static UEnum* ActorGridPlacementEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("EActorGridPlacement"));

				UE_LOG(LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("Clustered %d actors (%s%s%s), generated shared BV of [%d x %d] (meters)"),
					ActorCluster->Actors.Num(),
					*ActorGridPlacementEnum->GetNameStringByValue((int64)GridPlacement),
					bAlwaysLoadedPromotedCluster ? TEXT(":PromotedCluster") : TEXT(""),
					bAlwaysLoadedPromotedOutOfGrid ? TEXT(":PromotedOutOfGrid") : TEXT(""),
					(int)(0.01f * ClusterInstance->Bounds.GetSize().X),
					(int)(0.01f * ClusterInstance->Bounds.GetSize().Y));

				for (const FGuid& ActorGuid : ActorCluster->Actors)
				{
					const FWorldPartitionActorDescView& ActorDescView = ClusterInstance->ContainerInstance->GetActorDescView(ActorGuid);
					UE_LOG(LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("   - Actor: %s (%s)"), *ActorDescView.GetActorPath().ToString(), *ActorGuid.ToString(EGuidFormats::UniqueObjectGuid));
					UE_LOG(LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("         Package: %s"), *ActorDescView.GetActorPackage().ToString());
					UE_LOG(LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("         Container (%08x): %s"), ClusterInstance->ContainerInstance->ID, *ClusterInstance->ContainerInstance->Container->GetContainerPackage().ToString())
				}
			}
		}
	}

	// Perform validation
	PartitionedActors.ValidateSingleActorReferer();

	return PartitionedActors;
}

#endif // #if WITH_EDITOR
