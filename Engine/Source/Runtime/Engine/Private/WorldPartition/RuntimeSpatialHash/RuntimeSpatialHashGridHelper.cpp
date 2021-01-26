// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/RuntimeSpatialHash/RuntimeSpatialHashGridHelper.h"

#include "ProfilingDebugging/ScopedTimers.h"

FSquare2DGridHelper::FSquare2DGridHelper(int32 InNumLevels, const FVector& InOrigin, int32 InCellSize, int32 InGridSize)
	: Origin(InOrigin)
	, CellSize(InCellSize)
	, GridSize(InGridSize)
{
	Levels.Reserve(InNumLevels);

	int32 CurrentCellSize = CellSize;
	int32 CurrentGridSize = GridSize;

	const FVector2D BaseLevelOffset = 0.5 * FVector2D(CellSize, CellSize);
	for (int32 Level = 0; Level < InNumLevels; ++Level)
	{
		// Add offset on origin based on level's cell size to break pattern of perfectly aligned cell edges at multiple level.
		// This will prevent weird artefact during actor promotion.
		// Apply base level offset so that first level isn't offset.
		const FVector2D GridLevelOffset = 0.5f * FVector2D(CurrentCellSize, CurrentCellSize) - BaseLevelOffset;
		const FVector2D LevelOrigin = FVector2D(InOrigin) + GridLevelOffset;

		Levels.Emplace(LevelOrigin, CurrentCellSize, CurrentGridSize);

		CurrentCellSize <<= 1;
		CurrentGridSize >>= 1;
	}
}

void FSquare2DGridHelper::ForEachCells(TFunctionRef<void(const FIntVector&)> InOperation) const
{
	for (int32 Level = 0; Level < Levels.Num(); Level++)
	{
		Levels[Level].ForEachCells([Level, &InOperation](const FIntVector2& Coord) { InOperation(FIntVector(Coord.X, Coord.Y, Level)); });
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

	TSet<FGuid> ActorUsage;
	for (int32 Level = 0; Level < Levels.Num() - 1; Level++)
	{
		const int32 CurrentGridSize = Levels[Level].GridSize;
		for (int32 y = 0; y < CurrentGridSize; y++)
		{
			for (int32 x = 0; x < CurrentGridSize; x++)
			{
				FGridLevel::FGridCell& ThisCell = Levels[Level].Cells[y * CurrentGridSize + x];
				for (const FGridLevel::FGridCellDataChunk& DataChunk : ThisCell.GetDataChunks())
				{
					for (const FGuid& ActorGuid : DataChunk.GetActors())
					{
						bool bWasAlreadyInSet;
						ActorUsage.Add(ActorGuid, &bWasAlreadyInSet);
						check(!bWasAlreadyInSet);
					}
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
	int32 GridSize = 1;
	int32 GridLevelCount = 1;

	float WorldBoundsMaxExtent = 0.f;
	// If World bounds is valid, compute Grid's size and level count based on it
	if (WorldBounds.IsValid)
	{
		const FVector2D DistMin = FMath::Abs(FVector2D(WorldBounds.Min - GridOrigin));
		const FVector2D DistMax = FMath::Abs(FVector2D(WorldBounds.Max - GridOrigin));
		WorldBoundsMaxExtent = FMath::Max(DistMin.GetMax(), DistMax.GetMax());
	}
	if (WorldBoundsMaxExtent > 0.f)
	{
		GridSize = 2.f * FMath::CeilToFloat(WorldBoundsMaxExtent / GridCellSize);
		if (!FMath::IsPowerOfTwo(GridSize))
		{
			GridSize = FMath::Pow(2.f, FMath::CeilToFloat(FMath::Log2(static_cast<float>(GridSize))));
		}
		GridLevelCount = FMath::Log2(static_cast<float>(GridSize)) + 1;
	}
	else
	{
		UE_LOG(LogWorldPartitionRuntimeSpatialHash, Warning, TEXT("Invalid world bounds, grid partitioning will use a runtime grid with 1 cell."));
	}

	return FSquare2DGridHelper(GridLevelCount, GridOrigin, GridCellSize, GridSize);
}

FSquare2DGridHelper GetPartitionedActors(const UWorldPartition* WorldPartition, const FBox& WorldBounds, const FSpatialHashRuntimeGrid& Grid, const TArray<FActorCluster>& GridActors)
{
	UE_SCOPED_TIMER(TEXT("GetPartitionedActors"), LogWorldPartitionRuntimeSpatialHash);

	//
	// Create the hierarchical grids for the game
	//	
	FSquare2DGridHelper PartitionedActors = GetGridHelper(WorldBounds, Grid.CellSize);

	for (const FActorCluster& ActorCluster : GridActors)
	{
		check(ActorCluster.Actors.Num() > 0);

		EActorGridPlacement GridPlacement = ActorCluster.GridPlacement;
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
			check(ActorCluster.Actors.Num() == 1);
			const FGuid& ActorGuid = *ActorCluster.Actors.CreateConstIterator();
			const FWorldPartitionActorDesc& ActorDesc = *WorldPartition->GetActorDesc(ActorGuid);
			if (PartitionedActors.GetLowestLevel().GetCellCoords(FVector2D(ActorDesc.GetOrigin()), CellCoords))
			{
				TArray<const UDataLayer*> ActorDataLayers;
				if (const AWorldDataLayers* WorldDataLayers = AWorldDataLayers::Get(WorldPartition->GetWorld()))
				{
					for (const FName& DataLayerName : ActorDesc.GetDataLayers())
					{
						if (const UDataLayer* DataLayer = WorldDataLayers->GetDataLayerFromName(DataLayerName))
						{
							ActorDataLayers.Add(DataLayer);
						}
					}
				}
				PartitionedActors.GetLowestLevel().GetCell(CellCoords).AddActor(ActorGuid, ActorDataLayers);
			}
			else
			{
				GridPlacement = EActorGridPlacement::AlwaysLoaded;
				bAlwaysLoadedPromotedOutOfGrid = true;
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
				GridLevel.ForEachIntersectingCellsBreakable(ActorCluster.Bounds, [&IntersectingCellCount](const FIntVector2& Coords) { return ++IntersectingCellCount <= 1; });
				if (IntersectingCellCount == 1)
				{
					GridLevel.ForEachIntersectingCells(ActorCluster.Bounds, [&GridLevel, &ActorCluster](const FIntVector2& Coords)
						{
							GridLevel.GetCell(Coords).AddActors(ActorCluster.Actors, ActorCluster.DataLayers);
						});
					bFoundCell = true;
					break;
				}
			}
			if (!bFoundCell)
			{
				GridPlacement = EActorGridPlacement::AlwaysLoaded;
				bAlwaysLoadedPromotedOutOfGrid = true;
			}
			break;
		}
		case EActorGridPlacement::AlwaysLoaded:
		{
			PartitionedActors.GetAlwaysLoadedCell().AddActors(ActorCluster.Actors, ActorCluster.DataLayers);
			break;
		}
		default:
			check(0);
		}

		if (UE_LOG_ACTIVE(LogWorldPartitionRuntimeSpatialHash, Verbose))
		{
			if (ActorCluster.Actors.Num() > 1)
			{
				static UEnum* ActorGridPlacementEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("EActorGridPlacement"));

				UE_LOG(LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("Clustered %d actors (%s%s%s), generated shared BV of [%d x %d] (meters)"),
					ActorCluster.Actors.Num(),
					*ActorGridPlacementEnum->GetNameStringByValue((int64)GridPlacement),
					bAlwaysLoadedPromotedCluster ? TEXT(":PromotedCluster") : TEXT(""),
					bAlwaysLoadedPromotedOutOfGrid ? TEXT(":PromotedOutOfGrid") : TEXT(""),
					(int)(0.01f * ActorCluster.Bounds.GetSize().X),
					(int)(0.01f * ActorCluster.Bounds.GetSize().Y));

				for (const FGuid& ActorGuid : ActorCluster.Actors)
				{
					const FWorldPartitionActorDesc& Desc = *WorldPartition->GetActorDesc(ActorGuid);
					UE_LOG(LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("   - Actor: %s (%s)"), *Desc.GetActorPath().ToString(), *ActorGuid.ToString(EGuidFormats::UniqueObjectGuid));
					UE_LOG(LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("            %s"), *Desc.GetActorPackage().ToString());
				}
			}
		}
	}

	// Perform validation
	PartitionedActors.ValidateSingleActorReferer();

	return PartitionedActors;
}

#endif // #if WITH_EDITOR
