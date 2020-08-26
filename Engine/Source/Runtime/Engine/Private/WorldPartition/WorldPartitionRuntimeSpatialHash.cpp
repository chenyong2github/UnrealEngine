// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	WorldPartitionRuntimeSpatialHash.cpp: UWorldPartitionRuntimeSpatialHash implementation
=============================================================================*/
#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"
#include "WorldPartition/WorldPartitionRuntimeSpatialHashCell.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartitionActorDescIterator.h"
#include "WorldPartition/WorldPartitionStreamingPolicy.h"
#include "GameFramework/WorldSettings.h"
#include "Math/Grid2D.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Engine/World.h"
#include "Engine/Canvas.h"
#include "DrawDebugHelpers.h"
#include "DisplayDebugHelpers.h"
#include "RenderUtils.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "Misc/Parse.h"
#include "Algo/Find.h"
#include "Algo/Transform.h"

#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"

#include "AssetRegistryModule.h"
#include "AssetData.h"

#include "Engine/WorldComposition.h"
#include "LevelUtils.h"

extern UNREALED_API class UEditorEngine* GEditor;
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionRuntimeSpatialHash, Log, All);

#if WITH_EDITOR
class FScopedLoadActorsHelper
{
public:
	FScopedLoadActorsHelper(UWorldPartition* InWorldPartition, const TSet<FGuid>& InActors, bool bSkipEditorOnly)
		: WorldPartition(InWorldPartition)
	{
		LoadedActors.Reserve(InActors.Num());
		for (const FGuid& ActorGuid : InActors)
		{
			FWorldPartitionActorDesc* ActorDesc = WorldPartition->GetActorDesc(ActorGuid);
			if (!ActorDesc->GetActor() && (!bSkipEditorOnly || !ActorDesc->GetActorIsEditorOnly()))
			{
				AActor* Actor = ActorDesc->Load();
				if (ensure(Actor))
				{
					LoadedActors.Add(Actor);
				}
			}
		}
	}

	~FScopedLoadActorsHelper()
	{
		for (AActor* Actor : LoadedActors)
		{
			check(!Actor->IsPackageExternal());
			TGuardValue<ITransaction*> SuppressTransaction(GUndo, nullptr);
			WorldPartition->GetWorld()->DestroyActor(Actor, false, false);
		}
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
	}
private:
	UWorldPartition* WorldPartition;
	TArray<AActor*> LoadedActors;
};
#endif

/** 
  * Square 2D grid helper
  */
struct FSquare2DGridHelper
{
	struct FGridLevel : public FGrid2D
	{
		struct FGridCell
		{
			TSet<FGuid> Actors;
		};

		inline FGridLevel(const FVector2D& InOrigin, int32 InCellSize, int32 InGridSize)
			: FGrid2D(InOrigin, InCellSize, InGridSize)
		{
			Cells.InsertDefaulted(0, GridSize * GridSize);
		}

		/**
		 * Returns the cell at the specified grid coordinate
		 *
		 * @return the cell at the specified grid coordinate
		 */
		inline FGridCell& GetCell(const FIntVector2& InCoords)
		{
			check(IsValidCoords(InCoords));
			uint32 CellIndex;
			GetCellIndex(InCoords, CellIndex);
			return Cells[CellIndex];
		}

		/**
		 * Returns the cell at the specified grid coordinate
		 *
		 * @return the cell at the specified grid coordinate
		 */
		inline const FGridCell& GetCell(const FIntVector2& InCoords) const
		{
			check(IsValidCoords(InCoords));
			uint32 CellIndex;
			GetCellIndex(InCoords, CellIndex);
			return Cells[CellIndex];
		}

		TArray<FGridCell> Cells;
	};

	inline FSquare2DGridHelper(int32 InNumLevels, const FVector& InOrigin, int32 InCellSize, int32 InGridSize)
		: Origin(InOrigin)
		, CellSize(InCellSize)
		, GridSize(InGridSize)
	{
		Levels.Reserve(InNumLevels);

		int32 CurrentCellSize = InCellSize;
		int32 CurrentGridSize = InGridSize;

		for (int32 Level = 0; Level < InNumLevels; ++Level)
		{
			Levels.Emplace(FVector2D(InOrigin), CurrentCellSize, CurrentGridSize);

			CurrentCellSize <<= 1;
			CurrentGridSize >>= 1;
		}
	}

	/**
	 * Returns the lowest grid level
	 */
	inline FGridLevel& GetLowestLevel()
	{
		return Levels[0];
	}

	/**
	 * Returns the always loaded (top level) cell
	 */
	inline FGridLevel::FGridCell& GetAlwaysLoadedCell()
	{
		return Levels.Last().Cells[0];
	}

	/**
	 * Returns the always loaded (top level) cell
	 */
	inline const FGridLevel::FGridCell& GetAlwaysLoadedCell() const
	{
		return Levels.Last().Cells[0];
	}

	/**
	 * Returns the cell at the given coord
	 */
	inline const FGridLevel::FGridCell& GetCell(const FIntVector& InCoords) const
	{
		return Levels[InCoords.Z].GetCell(FIntVector2(InCoords.X, InCoords.Y));
	}

	/**
	 * Returns the cell bounds
	 *
	 * @return true if the specified coord was valid
	 */
	inline bool GetCellBounds(const FIntVector& InCoords, FBox2D& OutBounds) const
	{
		if (Levels.IsValidIndex(InCoords.Z))
		{
			return Levels[InCoords.Z].GetCellBounds(FIntVector2(InCoords.X, InCoords.Y), OutBounds);
		}

		return false;
	}

	/**
	 * Runs a function on all cells
	 */
	void ForEachCells(TFunctionRef<void(const FIntVector&)> InOperation) const
	{
		for (int32 Level = 0; Level < Levels.Num(); Level++)
		{
			const int32 CurrentGridSize = Levels[Level].GridSize;

			for (int32 y = 0; y < CurrentGridSize; y++)
			{
				for (int32 x = 0; x < CurrentGridSize; x++)
				{
					InOperation(FIntVector(x, y, Level));
				}
			}
		}
	}

	/**
	 * Runs a function on all intersecting cells for the provided box
	 *
	 * @return the number of intersecting cells
	 */
	int32 ForEachIntersectingCells(const FBox& InBox, TFunctionRef<void(const FIntVector&)> InOperation) const
	{
		int32 NumCells = 0;
	
		for (int32 Level = 0; Level < Levels.Num(); Level++)
		{
			NumCells += Levels[Level].ForEachIntersectingCells(InBox, [InBox, Level, InOperation](const FIntVector2& Coord) { InOperation(FIntVector(Coord.X, Coord.Y, Level)); });
		}

		return NumCells;
	}

	/**
	 * Runs a function on all intersecting cells for the provided sphere
	 *
	 * @return the number of intersecting cells
	 */
	int32 ForEachIntersectingCells(const FSphere& InSphere, TFunctionRef<void(const FIntVector&)> InOperation) const
	{
		int32 NumCells = 0;

		for (int32 Level = 0; Level < Levels.Num(); Level++)
		{
			NumCells += Levels[Level].ForEachIntersectingCells(InSphere, [InSphere, Level, InOperation](const FIntVector2& Coord) { InOperation(FIntVector(Coord.X, Coord.Y, Level)); });
		}

		return NumCells;
	}

#if WITH_EDITOR
	/**
	 * Perform actors promotion
	 * When an actor is referenced by multiple cells, promote it to the higher level parent cell.
	 */
	void PerformActorsPromotion()
	{
		UE_SCOPED_TIMER(TEXT("PerformActorsPromotion"), LogWorldPartitionRuntimeSpatialHash, Log);

		for (int32 Level = 0; Level < Levels.Num() - 1; Level++)
		{
			const int32 CurrentGridSize = Levels[Level].GridSize;

			// First pass: gather actors usage
			TMap<FGuid, int32> ActorUsage;
			for (int32 y = 0; y < CurrentGridSize; y++)
			{
				for (int32 x = 0; x < CurrentGridSize; x++)
				{
					FGridLevel::FGridCell& ThisCell = Levels[Level].GetCell(FIntVector2(x, y));
					for (const FGuid& ActorGuid : ThisCell.Actors)
					{
						ActorUsage.FindOrAdd(ActorGuid, 0)++;
					}
				}
			}

			// Second pass: promote actors to higher level
			for (int32 y = 0; y < CurrentGridSize; y++)
			{
				for (int32 x = 0; x < CurrentGridSize; x++)
				{
					FGridLevel::FGridCell& ThisCell = Levels[Level].GetCell(FIntVector2(x, y));
					for (auto ActorIt = ThisCell.Actors.CreateIterator(); ActorIt; ++ActorIt)
					{
						const FGuid& ActorGuid = *ActorIt;
						const int32 ActorUse = ActorUsage.FindChecked(ActorGuid);

						// @todo_ow: Use StreamingPolicy to get Minimum Actor count for promotion
						if (ActorUse > 1)
						{
							FIntVector2 ParentCellCoord(x/2, y/2);
							int32 ParentCellLevel = Level + 1;
							FGridLevel::FGridCell& ParentCell = Levels[ParentCellLevel].GetCell(ParentCellCoord);
							if (!ParentCell.Actors.Contains(ActorGuid))
							{
								ParentCell.Actors.Add(ActorGuid);
								UE_LOG(LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("Promoted %s(%d) from cell(X%02d, Y%02d, Level=%d) to cell(X%02d, Y%02d, Level=%d)"), *ActorGuid.ToString(EGuidFormats::UniqueObjectGuid), ActorUse, x, y, Level, ParentCellCoord.X, ParentCellCoord.Y, ParentCellLevel);
							}
							ActorIt.RemoveCurrent();
						}
					}
				}
			}
		}

		// Third pass: validate actor usage on all levels
		for (int32 Level = 0; Level < Levels.Num() - 1; Level++)
		{
			const int32 CurrentGridSize = Levels[Level].GridSize;

			TSet<FGuid> ActorUsage;
			for (int32 y = 0; y < CurrentGridSize; y++)
			{
				for (int32 x = 0; x < CurrentGridSize; x++)
				{
					FGridLevel::FGridCell& ThisCell = Levels[Level].Cells[y * CurrentGridSize + x];
					for (const FGuid& ActorGuid : ThisCell.Actors)
					{
						bool bWasAlreadyInSet;
						ActorUsage.Add(ActorGuid, &bWasAlreadyInSet);
						check(!bWasAlreadyInSet);
					}
				}
			}
		}
	}
#endif

public:
	FVector Origin;
	int32 CellSize;
	int32 GridSize;
	TArray<FGridLevel> Levels;
};

#if WITH_EDITOR

static FSquare2DGridHelper GetPartitionedActors(const UWorldPartition* WorldPartition, const FBox& WorldBounds, const FSpatialHashRuntimeGrid& Grid, const TArray<UWorldPartition::FActorCluster>& GridActors)
{
	UE_SCOPED_TIMER(TEXT("GetPartitionedActors"), LogWorldPartitionRuntimeSpatialHash, Log);

	// Default grid to a minimum of 1 level and 1 cell, for always loaded actors
	const int32 GridCellSize = Grid.CellSize;
	FVector GridOrigin = FVector::ZeroVector;
	int32 GridSize = 1;
	int32 GridLevelCount = 1;
	
	// If World bounds is valid, compute Grid's origin, size and level count based on it
	const float WorldBoundsMaxExtent = WorldBounds.IsValid ? WorldBounds.GetExtent().GetMax() : 0.f;
	if (WorldBoundsMaxExtent > 0.f)
	{
		GridOrigin = WorldBounds.GetCenter();
		GridSize = 2.f * FMath::CeilToFloat(WorldBoundsMaxExtent / GridCellSize);
		if (!FMath::IsPowerOfTwo(GridSize))
		{
			GridSize = FMath::Pow(2.f, FMath::CeilToFloat(FMath::Log2(GridSize)));
		}
		GridLevelCount = FMath::Log2(GridSize) + 1;
	}
	else
	{
		UE_LOG(LogWorldPartitionRuntimeSpatialHash, Warning, TEXT("Invalid world bounds, grid partitioning will use a runtime grid with 1 cell."));
	}

	//
	// Create the hierarchical grids for the game
	//	
	FSquare2DGridHelper PartitionedActors(GridLevelCount, GridOrigin, GridCellSize, GridSize);

	for (const UWorldPartition::FActorCluster& ActorCluster : GridActors)
	{
		check(ActorCluster.Actors.Num() > 0);

		EActorGridPlacement GridPlacement = ActorCluster.GridPlacement;
		bool bAlwaysLoadedPromotedCluster = (GridPlacement == EActorGridPlacement::None);
		bool bAlwaysLoadedPromotedOutOfGrid = false;

		if (bAlwaysLoadedPromotedCluster)
		{
			GridPlacement = EActorGridPlacement::AlwaysLoaded;
		}

		if (GridPlacement != EActorGridPlacement::AlwaysLoaded)
		{
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
					PartitionedActors.GetLowestLevel().GetCell(CellCoords).Actors.Add(ActorGuid);
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
				const FBox ActorClusterBounds = WorldPartition->GetActorClusterBounds(ActorCluster);
				if (!PartitionedActors.GetLowestLevel().ForEachIntersectingCells(ActorClusterBounds, [&](const FIntVector2& Coords)
				{
					PartitionedActors.GetLowestLevel().GetCell(Coords).Actors.Append(ActorCluster.Actors);
				}))
				{
					GridPlacement = EActorGridPlacement::AlwaysLoaded;
					bAlwaysLoadedPromotedOutOfGrid = true;
				}
				break;
			}
			default:
				check(0);
			}
		}
			
		if (GridPlacement == EActorGridPlacement::AlwaysLoaded)
		{
			PartitionedActors.GetAlwaysLoadedCell().Actors.Append(ActorCluster.Actors);
		}

		if (!LogWorldPartitionRuntimeSpatialHash.IsSuppressed(ELogVerbosity::Verbose))
		{
			if (ActorCluster.Actors.Num() > 1)
			{
				static UEnum* ActorGridPlacementEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("EActorGridPlacement"));
				const FBox ActorClusterBounds = WorldPartition->GetActorClusterBounds(ActorCluster);

				UE_LOG(LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("Clustered %d actors (%s%s%s), generated shared BV of [%d x %d] (meters)"), 
					ActorCluster.Actors.Num(),
					*ActorGridPlacementEnum->GetNameStringByValue((int64)GridPlacement),
					bAlwaysLoadedPromotedCluster ? TEXT(":PromotedCluster") : TEXT(""),
					bAlwaysLoadedPromotedOutOfGrid ? TEXT(":PromotedOutOfGrid") : TEXT(""),
					(int)(0.01f * ActorClusterBounds.GetSize().X),
					(int)(0.01f * ActorClusterBounds.GetSize().Y));

				for (const FGuid& ActorGuid : ActorCluster.Actors)
				{
					const FWorldPartitionActorDesc& Desc = *WorldPartition->GetActorDesc(ActorGuid);
					UE_LOG(LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("   - Actor: %s (%s)"), *Desc.GetActorPath().ToString(), *ActorGuid.ToString(EGuidFormats::UniqueObjectGuid));
					UE_LOG(LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("            %s"), *Desc.GetActorPackage().ToString());
				}
			}
		}
	}

	// Perform actor promotion
	PartitionedActors.PerformActorsPromotion();
	
	return PartitionedActors;
}
#endif

UWorldPartitionRuntimeSpatialHash::UWorldPartitionRuntimeSpatialHash(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}

#if WITH_EDITOR
FName UWorldPartitionRuntimeSpatialHash::GetActorRuntimeGrid(const AActor* Actor) const
{
	if (ULevel* Level = Actor ? Actor->GetLevel() : nullptr)
	{
		if (const FName* ActorRuntimeGrid = WorldCompositionStreamingLevelToRuntimeGrid.Find(FLevelUtils::FindStreamingLevel(Level)))
		{
			return *ActorRuntimeGrid;
		}
	}
	return Super::GetActorRuntimeGrid(Actor);
}

void UWorldPartitionRuntimeSpatialHash::SetDefaultValues()
{
	FSpatialHashRuntimeGrid& MainGrid = Grids.AddDefaulted_GetRef();
	MainGrid.GridName = TEXT("MainGrid");
	MainGrid.CellSize = 3200;
	MainGrid.LoadingRange = 25600;
	MainGrid.DebugColor = FLinearColor::Gray;
}

void UWorldPartitionRuntimeSpatialHash::ImportFromWorldComposition(UWorldComposition* WorldComposition)
{
	check(IsRunningCommandlet());

	if (WorldComposition)
	{
		const TArray<FWorldTileLayer> WorldCompositionTileLayers = WorldComposition->GetDistanceDependentLayers();
		for (const FWorldTileLayer& Layer : WorldCompositionTileLayers)
		{
			FName GridName = FName(Layer.Name);
			FSpatialHashRuntimeGrid* Grid = Algo::FindByPredicate(Grids, [GridName](const FSpatialHashRuntimeGrid& Grid) { return Grid.GridName == GridName; });
			if (!Grid)
			{
				Grid = &Grids.AddDefaulted_GetRef();
				Grid->GridName = GridName;
				Grid->CellSize = 3200;
				Grid->DebugColor = FLinearColor::MakeRandomColor();
			}
			// World Composition Layer Streaming Distance always wins over existing value (config file)
			Grid->LoadingRange = Layer.StreamingDistance;
		}

		const UWorldComposition::FTilesList& Tiles = WorldComposition->GetTilesList();
		for (int32 TileIdx = 0; TileIdx < Tiles.Num(); TileIdx++)
		{
			const FWorldCompositionTile& Tile = Tiles[TileIdx];
			ULevelStreaming* StreamingLevel = WorldComposition->TilesStreaming[TileIdx];
			if (StreamingLevel && WorldComposition->IsDistanceDependentLevel(Tile.PackageName))
			{
				// Map WorldComposition tiles streaming level to Runtime Grid
				WorldCompositionStreamingLevelToRuntimeGrid.Add(StreamingLevel, FName(Tile.Info.Layer.Name));
			}
		}
	}
}

bool UWorldPartitionRuntimeSpatialHash::GenerateStreaming(EWorldPartitionStreamingMode Mode, UWorldPartitionStreamingPolicy* StreamingPolicy)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionRuntimeSpatialHash::GenerateStreaming);
	UWorldPartition* WorldPartition = GetOuterUWorldPartition();
	check(!WorldPartition->IsPreCooked());

	UE_SCOPED_TIMER(TEXT("GenerateStreaming"), LogWorldPartitionRuntimeSpatialHash, Log);
	
	if (!Grids.Num())
	{
		UE_LOG(LogWorldPartitionRuntimeSpatialHash, Error, TEXT("Invalid partition grids setup"));
		return false;
	}

	TArray<FSpatialHashRuntimeGrid> AllGrids;
	AllGrids.Append(Grids);
	AllGrids.Append(HLODGrids);

	check(!StreamingGrids.Num());

	// Build a map of Actor GUID -> HLODActor GUID once instead of having to recompute for every streaming grid we create
	CacheHLODParents();

	TMap<FName, int32> GridsMapping;
	GridsMapping.Add(NAME_None, 0);
	for (int32 i = 0; i < AllGrids.Num(); i++)
	{
		const FSpatialHashRuntimeGrid& Grid = AllGrids[i];
		check(!GridsMapping.Contains(Grid.GridName));
		GridsMapping.Add(Grid.GridName, i);
	}

	// Create actor clusters
	TArray<UWorldPartition::FActorCluster> ActorClusters = WorldPartition->CreateActorClusters();

	TArray<TArray<UWorldPartition::FActorCluster>> GridActors;
	GridActors.InsertDefaulted(0, AllGrids.Num());

	for (UWorldPartition::FActorCluster& ActorCluster : ActorClusters)
	{
		int32* FoundIndex = GridsMapping.Find(ActorCluster.RuntimeGrid);
		if (!FoundIndex)
		{
			UE_LOG(LogWorldPartitionRuntimeSpatialHash, Error, TEXT("Invalid partition grid '%s' referenced by actor cluster"), *ActorCluster.RuntimeGrid.ToString());
		}

		int32 GridIndex = FoundIndex ? *FoundIndex : 0;
		GridActors[GridIndex].Add(MoveTemp(ActorCluster));
	}
	
	const FBox WorldBounds = WorldPartition->GetWorldBounds();
	for (int32 GridIndex=0; GridIndex < AllGrids.Num(); GridIndex++)
	{
		const FSpatialHashRuntimeGrid& Grid = AllGrids[GridIndex];
		const FSquare2DGridHelper PartionedActors = GetPartitionedActors(WorldPartition, WorldBounds, Grid, GridActors[GridIndex]);
		if (!CreateStreamingGrid(Grid, PartionedActors, Mode, StreamingPolicy))
		{
			return false;
		}
	}

	return true;
}

FName UWorldPartitionRuntimeSpatialHash::GetCellName(FName InGridName, int32 InLevel, int32 InCellX, int32 InCellY) const
{
	UWorldPartition* WorldPartition = GetOuterUWorldPartition();
	const FString PackageName = FPackageName::GetShortName(WorldPartition->GetPackage());
	const FString PackageNameNoPIEPrefix = UWorld::RemovePIEPrefix(PackageName);

	return FName(*FString::Printf(TEXT("WPRT_%s_%s_Cell_L%d_X%02d_Y%02d"), *PackageNameNoPIEPrefix, *InGridName.ToString(), InLevel, InCellX, InCellY));
}

#if WITH_EDITOR
void UWorldPartitionRuntimeSpatialHash::CacheHLODParents()
{
	CachedHLODParents.Reset();

	UWorldPartition* WorldPartition = GetOuterUWorldPartition();

	for (TWorldPartitionActorDescIterator<AWorldPartitionHLOD, FHLODActorDesc> HLODIterator(WorldPartition); HLODIterator; ++HLODIterator)
	{
		for (const auto& SubActor : HLODIterator->GetSubActors())
		{
			CachedHLODParents.Emplace(SubActor, HLODIterator->GetGuid());
		}
	}
}

bool UWorldPartitionRuntimeSpatialHash::CreateStreamingGrid(const FSpatialHashRuntimeGrid& RuntimeGrid, const FSquare2DGridHelper& PartionedActors, EWorldPartitionStreamingMode Mode, UWorldPartitionStreamingPolicy* StreamingPolicy)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionRuntimeSpatialHash::CreateStreamingGrid);

	UWorldPartition* WorldPartition = GetOuterUWorldPartition();

	check(FMath::IsPowerOfTwo(PartionedActors.GridSize));
	FSpatialHashStreamingGrid& CurrentStreamingGrid = StreamingGrids.AddDefaulted_GetRef();
	CurrentStreamingGrid.GridName = RuntimeGrid.GridName;
	CurrentStreamingGrid.Origin = PartionedActors.Origin;
	CurrentStreamingGrid.CellSize = PartionedActors.CellSize;
	CurrentStreamingGrid.GridSize = PartionedActors.GridSize;
	CurrentStreamingGrid.LoadingRange = RuntimeGrid.LoadingRange;
	CurrentStreamingGrid.DebugColor = RuntimeGrid.DebugColor;

	// Move actors into the final streaming grids
	CurrentStreamingGrid.GridLevels.Reserve(PartionedActors.Levels.Num());

	int32 Level = INDEX_NONE;
	for (const FSquare2DGridHelper::FGridLevel& TempLevel : PartionedActors.Levels)
	{
		Level++;

		FSpatialHashStreamingGridLevel& GridLevel = CurrentStreamingGrid.GridLevels.AddDefaulted_GetRef();

		GridLevel.GridCells.Reserve(TempLevel.Cells.Num());

		int32 CellIndex = INDEX_NONE;
		for (const FSquare2DGridHelper::FGridLevel::FGridCell& TempCell : TempLevel.Cells)
		{
			CellIndex++;
			if (!TempCell.Actors.Num())
			{
				GridLevel.GridCells.Add(nullptr);
				continue;
			}

			const bool bIsCellAlwaysLoaded = &TempCell == &PartionedActors.GetAlwaysLoadedCell();

			const int32 CellCoordX = CellIndex % TempLevel.GridSize;
			const int32 CellCoordY = CellIndex / TempLevel.GridSize;
			FName CellName = GetCellName(CurrentStreamingGrid.GridName, Level, CellCoordX, CellCoordY);

			UWorldPartitionRuntimeSpatialHashCell* StreamingCell = NewObject<UWorldPartitionRuntimeSpatialHashCell>(WorldPartition, StreamingPolicy->GetRuntimeCellClass(), CellName);
			GridLevel.GridCells.Add(StreamingCell);
			StreamingCell->SetIsAlwaysLoaded(bIsCellAlwaysLoaded);
			StreamingCell->Level = Level;
			FBox2D Bounds;
			verify(TempLevel.GetCellBounds(FIntVector2(CellCoordX, CellCoordY), Bounds));
			StreamingCell->Position = FVector(Bounds.GetCenter(), 0.f);

			UE_LOG(LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("Cell%s %s Actors = %d"), bIsCellAlwaysLoaded ? TEXT(" (AlwaysLoaded)") : TEXT(""), *StreamingCell->GetName(), TempCell.Actors.Num());

			// Keep track of all AWorldPartitionHLOD actors referenced by this cell
			TSet<FGuid> ReferencedHLODActors;

			for (const FGuid& ActorGuid : TempCell.Actors)
			{
				const FWorldPartitionActorDesc* ActorDesc = WorldPartition->GetActorDesc(ActorGuid);
				const bool bShouldStripActorFromStreaming = ActorDesc->GetActorIsEditorOnly();

				if (!bShouldStripActorFromStreaming)
				{
					FGuid ParentHLOD = CachedHLODParents.FindRef(ActorGuid);
					if (ParentHLOD.IsValid())
					{
						ReferencedHLODActors.Add(ParentHLOD);
					}

					StreamingCell->AddActorToCell(ActorDesc->GetActorPackage(), ActorDesc->GetActorPath());
					UE_LOG(LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("  Actor : %s (%s) Origin(%s)"), *(ActorDesc->GetActorPath().ToString()), *ActorGuid.ToString(EGuidFormats::UniqueObjectGuid), *FVector2D(ActorDesc->GetOrigin()).ToString());
				}
				else
				{
					UE_LOG(LogWorldPartitionRuntimeSpatialHash, Verbose, TEXT("  Stripping Actor %s (%s) from streaming grid"), *(ActorDesc->GetActorPath().ToString()), *ActorGuid.ToString(EGuidFormats::UniqueObjectGuid));
				}
			}

			if (ReferencedHLODActors.Num() > 0)
			{
				// Store the referenced HLOD actors as custom cell data
				UWorldPartitionRuntimeHLODCellData* HLODCellData = NewObject<UWorldPartitionRuntimeHLODCellData>(StreamingCell);
				HLODCellData->SetReferencedHLODActors(ReferencedHLODActors.Array());
				StreamingCell->AddCellData(HLODCellData);
			}
			
			if (Mode == EWorldPartitionStreamingMode::RuntimeStreamingCells)
			{
				FScopedLoadActorsHelper LoadCellActors(WorldPartition, TempCell.Actors, /*bSkipEditorOnly*/true);
				UE_LOG(LogWorldPartitionRuntimeSpatialHash, Log, TEXT("Creating runtime streaming cells %s."), *StreamingCell->GetName());
				if (!StreamingCell->CreateCellForCook())
				{
					UE_LOG(LogWorldPartitionRuntimeSpatialHash, Error, TEXT("Error creating runtime streaming cells for cook."));
					return false;
				}
			}
		}
	}

	return true;
}
#endif

void UWorldPartitionRuntimeSpatialHash::FlushStreaming()
{
	check(!GetOuterUWorldPartition()->IsPreCooked());
	StreamingGrids.Empty();
}

bool UWorldPartitionRuntimeSpatialHash::GenerateHLOD()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionRuntimeSpatialHash::GenerateHLOD);

	FAutoScopedDurationTimer Timer;

	if (!Grids.Num())
	{
		UE_LOG(LogWorldPartitionRuntimeSpatialHash, Error, TEXT("Invalid partition grids setup"));
		return false;
	}

	// Recreate HLODGrids from known layers
	HLODGrids.Reset();

	// Gather up all HLODLayer assets
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> HLODLayerAssets;
	AssetRegistryModule.Get().GetAssetsByClass(UHLODLayer::StaticClass()->GetFName(), HLODLayerAssets);

	// Ensure all assets are loaded
	for (const FAssetData& HLODLayerAsset : HLODLayerAssets)
	{
		if (UHLODLayer* HLODLayer = Cast<UHLODLayer>(HLODLayerAsset.GetAsset()))
		{
			for (const FHLODLevelSettings& HLODLevel : HLODLayer->GetLevels())
			{
				check(HLODLevel.TargetGrid != NAME_None);
				check(HLODLevel.LoadingRange > 0);

				FSpatialHashRuntimeGrid& HLODGrid = HLODGrids.AddDefaulted_GetRef();
				HLODGrid.GridName = HLODLevel.TargetGrid;
				HLODGrid.CellSize = HLODLevel.LoadingRange * 2;	// @todo_ow: Proper setup
				HLODGrid.LoadingRange = HLODLevel.LoadingRange;
				HLODGrid.DebugColor = FLinearColor::Red;
			}
		}
	}

	TMap<FName, int32> GridsMapping;
	GridsMapping.Add(NAME_None, 0);
	for (int32 i = 0; i < Grids.Num(); i++)
	{
		const FSpatialHashRuntimeGrid& Grid = Grids[i];
		check(!GridsMapping.Contains(Grid.GridName));
		GridsMapping.Add(Grid.GridName, i);
	}

	UWorldPartition* WorldPartition = GetOuterUWorldPartition();

	// Create actor clusters
	TArray<UWorldPartition::FActorCluster> ActorClusters = WorldPartition->CreateActorClusters();

	TArray<TArray<UWorldPartition::FActorCluster>> GridActors;
	GridActors.InsertDefaulted(0, Grids.Num());

	for (UWorldPartition::FActorCluster& ActorCluster : ActorClusters)
	{
		int32* FoundIndex = GridsMapping.Find(ActorCluster.RuntimeGrid);
		if (!FoundIndex)
		{
			UE_LOG(LogWorldPartitionRuntimeSpatialHash, Error, TEXT("Invalid partition grid '%s' referenced by actor cluster"), *ActorCluster.RuntimeGrid.ToString());
		}

		int32 GridIndex = FoundIndex ? *FoundIndex : 0;
		GridActors[GridIndex].Add(MoveTemp(ActorCluster));
	}

	const FBox WorldBounds = WorldPartition->GetWorldBounds();
	for (int32 GridIndex = 0; GridIndex < Grids.Num(); GridIndex++)
	{
		const FSpatialHashRuntimeGrid& RuntimeGrid = Grids[GridIndex];
		const FSquare2DGridHelper PartionedActors = GetPartitionedActors(WorldPartition, WorldBounds, RuntimeGrid, GridActors[GridIndex]);

		PartionedActors.ForEachCells([PartionedActors, RuntimeGrid, WorldPartition, this](const FIntVector& CellCoord)
		{
			const FSquare2DGridHelper::FGridLevel::FGridCell& GridCell = PartionedActors.GetCell(CellCoord);
			if (GridCell.Actors.Num() != 0)
			{
				FBox2D CellBounds;
				PartionedActors.GetCellBounds(CellCoord, CellBounds);

				FName CellName = GetCellName(RuntimeGrid.GridName, CellCoord.Z, CellCoord.X, CellCoord.Y);
				UHLODLayer::GenerateHLODForCell(WorldPartition, CellName, GridCell.Actors);
			}
		});
	}

	return true;
}
#endif

static int32 GShowRuntimeSpatialHashGridLevel = 0;
static FAutoConsoleVariableRef CVarGuardBandMultiplier(
	TEXT("WorldPartitionRuntimeSpatialHash.ShowGridLevel"),
	GShowRuntimeSpatialHashGridLevel,
	TEXT("Used to choose which grid level to display when showing world partition runtime grids."));

FAutoConsoleCommand UWorldPartitionRuntimeSpatialHash::OverrideLoadingRangeCommand(
	TEXT("WorldPartitionRuntimeSpatialHash.OverrideLoadingRange"),
	TEXT("Sets runtime loading range. Args -grid=[index] -range=[override_loading_range]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		FString ArgString = FString::Join(Args, TEXT(" "));
		int32 GridIndex = 0;
		float OverrideLoadingRange = 0.f;
		FParse::Value(*ArgString, TEXT("grid="), GridIndex);
		FParse::Value(*ArgString, TEXT("range="), OverrideLoadingRange);

		if (OverrideLoadingRange > 0.f)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World && World->IsGameWorld())
				{
					if (UWorldPartition* WorldPartition = World->GetWorldPartition())
					{
						if (UWorldPartitionRuntimeSpatialHash* RuntimeSpatialHash = Cast<UWorldPartitionRuntimeSpatialHash>(WorldPartition->RuntimeHash))
						{
							if (RuntimeSpatialHash->StreamingGrids.IsValidIndex(GridIndex))
							{
								RuntimeSpatialHash->StreamingGrids[GridIndex].OverrideLoadingRange = OverrideLoadingRange;
							}
						}
					}
				}
			}
		}
	})
);

// Streaming interface
int32 UWorldPartitionRuntimeSpatialHash::GetAllStreamingCells(TSet<const UWorldPartitionRuntimeCell*>& Cells) const
{
	for (const FSpatialHashStreamingGrid& StreamingGrid : StreamingGrids)
	{
		for (const FSpatialHashStreamingGridLevel& GridLevel : StreamingGrid.GridLevels)
		{
			for (const UWorldPartitionRuntimeSpatialHashCell* Cell : GridLevel.GridCells)
			{
				if (Cell)
				{
					Cells.Add(Cell);
				}
			}
		}
	}

	return Cells.Num();
}

int32 UWorldPartitionRuntimeSpatialHash::GetStreamingCells(const TArray<FWorldPartitionStreamingSource>& Sources, TSet<const UWorldPartitionRuntimeCell*>& Cells) const
{
	if (Sources.Num() == 0)
	{
		// Get always loaded cells
		for (const FSpatialHashStreamingGrid& StreamingGrid : StreamingGrids)
		{
			GetAlwaysLoadedStreamingCells(StreamingGrid, Cells);
		}
	}
	else
	{
		// Get cells based on streaming sources
		for (const FSpatialHashStreamingGrid& StreamingGrid : StreamingGrids)
		{
			for (const FWorldPartitionStreamingSource& Source : Sources)
			{
				GetStreamingCells(Source.Location, StreamingGrid, Cells);
			}
		}
	}

	return Cells.Num();
}

void UWorldPartitionRuntimeSpatialHash::GetStreamingCells(const FVector& Position, const FSpatialHashStreamingGrid& StreamingGrid, TSet<const UWorldPartitionRuntimeCell*>& Cells) const
{
	const FSphere GridSphere(Position, StreamingGrid.GetLoadingRange());
	const FSquare2DGridHelper GridHelper(StreamingGrid.GridLevels.Num(), StreamingGrid.Origin, StreamingGrid.CellSize, StreamingGrid.GridSize);

	GridHelper.ForEachIntersectingCells(GridSphere, [&](const FIntVector& Coords)
	{
		if (UWorldPartitionRuntimeSpatialHashCell* Cell = StreamingGrid.GridLevels[Coords.Z].GridCells[Coords.Y * GridHelper.Levels[Coords.Z].GridSize + Coords.X])
		{
			Cells.Add(Cell);
		}
	});
}

void UWorldPartitionRuntimeSpatialHash::GetAlwaysLoadedStreamingCells(const FSpatialHashStreamingGrid& StreamingGrid, TSet<const UWorldPartitionRuntimeCell*>& Cells) const
{
	if (StreamingGrid.GridLevels.Num() > 0)
	{
		const int32 TopLevel = StreamingGrid.GridLevels.Num() - 1;
		check(StreamingGrid.GridLevels[TopLevel].GridCells.Num() == 1);
		if (UWorldPartitionRuntimeSpatialHashCell* Cell = StreamingGrid.GridLevels[TopLevel].GridCells[0])
		{
			Cells.Add(Cell);
		}
	}
}

void UWorldPartitionRuntimeSpatialHash::SortStreamingCellsByDistance(const TSet<const UWorldPartitionRuntimeCell*>& InCells, const TArray<FWorldPartitionStreamingSource>& InSources, TArray<const UWorldPartitionRuntimeCell*>& OutSortedCells)
{
	struct FCellShortestDist
	{
		FCellShortestDist(const UWorldPartitionRuntimeSpatialHashCell* InCell, float InMinDistance)
			: Cell(InCell)
			, SourceMinDistance(InMinDistance)
		{}
		const UWorldPartitionRuntimeSpatialHashCell* Cell;
		float SourceMinDistance;
	};

	TArray<FCellShortestDist> SortedCells;
	SortedCells.Reserve(InCells.Num());
	for (const UWorldPartitionRuntimeCell* ToLoadCell : InCells)
	{
		const UWorldPartitionRuntimeSpatialHashCell* Cell = Cast<const UWorldPartitionRuntimeSpatialHashCell>(ToLoadCell);
		FCellShortestDist& SortedCell = SortedCells.Emplace_GetRef(Cell, FLT_MAX);

		for (const FWorldPartitionStreamingSource& Source : InSources)
		{
			float SqrDistance = FVector::DistSquared(Source.Location, Cell->Position);
			SortedCell.SourceMinDistance = FMath::Min(SqrDistance, SortedCell.SourceMinDistance);
		}
	}

	Algo::Sort(SortedCells, [](const FCellShortestDist& A, const FCellShortestDist& B)
	{
		if (A.Cell->Level == B.Cell->Level)
		{
			return A.SourceMinDistance < B.SourceMinDistance;
		}
		return A.Cell->Level > B.Cell->Level;
	});

	OutSortedCells.Reserve(InCells.Num());
	for (const FCellShortestDist& SortedCell : SortedCells)
	{
		OutSortedCells.Add(SortedCell.Cell);
	}
}

FVector2D UWorldPartitionRuntimeSpatialHash::GetShowDebugDesiredFootprint(const FVector2D& CanvasSize) const
{
	return FVector2D(CanvasSize.X * StreamingGrids.Num(), CanvasSize.Y);
}

void UWorldPartitionRuntimeSpatialHash::ShowDebugInfo(UCanvas* Canvas, const TArray<FWorldPartitionStreamingSource>& Sources, const FVector2D& PartitionCanvasOffset, const FVector2D& PartitionCanvasSize) const
{
	if (StreamingGrids.Num() == 0 || Sources.Num() == 0)
	{
		return;
	}

	UWorldPartition* WorldPartition = GetOuterUWorldPartition();

	const float CanvasMaxScreenSize = PartitionCanvasSize.X;
	const float GridMaxScreenSize = CanvasMaxScreenSize / StreamingGrids.Num();
	const float GridEffectiveScreenRatio = 1.f;
	const float GridEffectiveScreenSize = FMath::Min(GridMaxScreenSize, PartitionCanvasSize.Y) - 10.f;
	const float GridViewLoadingRangeExtentRatio = 1.5f;
	const float GridViewMinimumSizeInCellCount = 5.f;
	const FVector2D GridScreenExtent = FVector2D(GridEffectiveScreenSize, GridEffectiveScreenSize);
	const FVector2D GridScreenHalfExtent = 0.5f * GridScreenExtent;
	const FVector2D GridScreenInitialOffset = PartitionCanvasOffset;

	int32 GridIndex = 0;
	for (const FSpatialHashStreamingGrid& StreamingGrid : StreamingGrids)
	{
		const FVector2D GridScreenOffset = GridScreenInitialOffset + ((float)GridIndex * FVector2D(GridMaxScreenSize, 0.f)) + GridScreenHalfExtent;
		const FBox2D GridScreenBounds(GridScreenOffset - GridScreenHalfExtent, GridScreenOffset + GridScreenHalfExtent);

		// Display view sides based on extended grid loading range (minimum of N cells)
		const float GridSideDistance = FMath::Max((2.f * StreamingGrid.GetLoadingRange() * GridViewLoadingRangeExtentRatio), StreamingGrid.CellSize * GridViewMinimumSizeInCellCount);
		FSphere AverageSphere(ForceInit);
		for (const FWorldPartitionStreamingSource& Source : Sources)
		{
			AverageSphere += FSphere(Source.Location, 0.5f * GridSideDistance);
		}
		const FVector2D GridReferenceWorldPos = FVector2D(AverageSphere.Center);
		const float WorldToScreenScale = (0.5f * GridEffectiveScreenSize) / AverageSphere.W;
		auto WorldToScreen = [&](FVector2D WorldPos) { return (WorldToScreenScale * (WorldPos - GridReferenceWorldPos)) + GridScreenOffset; };

		// Draw Grid cells at desired grid level
		int32 GridLevel = FMath::Clamp<int32>(GShowRuntimeSpatialHashGridLevel, 0, StreamingGrid.GridLevels.Num() - 1);
		const FSquare2DGridHelper GridHelper(StreamingGrid.GridLevels.Num(), StreamingGrid.Origin, StreamingGrid.CellSize, StreamingGrid.GridSize);
		FBox TestBox(AverageSphere.Center - AverageSphere.W, AverageSphere.Center + AverageSphere.W);
		GridHelper.Levels[GridLevel].ForEachIntersectingCells(TestBox, [&](const FIntVector2& Coords)
		{
			FBox2D CellWorldBounds;
			GridHelper.Levels[GridLevel].GetCellBounds(FIntVector2(Coords.X, Coords.Y), CellWorldBounds);
			FBox2D CellScreenBounds = FBox2D(WorldToScreen(CellWorldBounds.Min), WorldToScreen(CellWorldBounds.Max));
			// Clamp inside grid bounds
			if (!GridScreenBounds.IsInside(CellScreenBounds))
			{
				CellScreenBounds.Min.X = FMath::Clamp(CellScreenBounds.Min.X, GridScreenBounds.Min.X, GridScreenBounds.Max.X);
				CellScreenBounds.Min.Y = FMath::Clamp(CellScreenBounds.Min.Y, GridScreenBounds.Min.Y, GridScreenBounds.Max.Y);
				CellScreenBounds.Max.X = FMath::Clamp(CellScreenBounds.Max.X, GridScreenBounds.Min.X, GridScreenBounds.Max.X);
				CellScreenBounds.Max.Y = FMath::Clamp(CellScreenBounds.Max.Y, GridScreenBounds.Min.Y, GridScreenBounds.Max.Y);
			}
			else
			{
				FString GridInfoText = FString::Printf(TEXT("X%02d_Y%02d"), Coords.X, Coords.Y);
				float TextWidth, TextHeight;
				Canvas->SetDrawColor(255, 255, 0);
				Canvas->StrLen(GEngine->GetTinyFont(), GridInfoText, TextWidth, TextHeight);
				FVector2D CellSize = CellScreenBounds.GetSize();
				if (TextWidth < CellSize.X && TextHeight < CellSize.Y)
				{
					FVector2D GridInfoPos = CellScreenBounds.GetCenter() - FVector2D(TextWidth / 2, TextHeight / 2);
					Canvas->DrawText(GEngine->GetTinyFont(), GridInfoText, GridInfoPos.X, GridInfoPos.Y);
				}
			}
			
			const UWorldPartitionRuntimeSpatialHashCell* Cell = Cast<const UWorldPartitionRuntimeSpatialHashCell>(StreamingGrid.GridLevels[GridLevel].GridCells[Coords.Y * GridHelper.Levels[GridLevel].GridSize + Coords.X]);
			FLinearColor CellColor = Cell ? Cell->GetDebugColor() : FLinearColor(0.f, 0.f, 0.f, 0.25f);
			FCanvasTileItem Item(CellScreenBounds.Min, GWhiteTexture, CellScreenBounds.GetSize(), CellColor);
			Item.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Item);

			FCanvasBoxItem Box(CellScreenBounds.Min, CellScreenBounds.GetSize());
			Box.SetColor(CellColor);
			Box.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Box);
		});

		// Draw Loading Ranges
		float LoadingRange = StreamingGrid.GetLoadingRange();

		FCanvasLineItem LineItem;
		LineItem.LineThickness = 2;
		LineItem.SetColor(FLinearColor::White);

		FCanvas* CanvasObject = Canvas->Canvas;

		for (const FWorldPartitionStreamingSource& Source : Sources)
		{
			TArray<FVector> LinePoints;
			LinePoints.SetNum(2);

			float Sin, Cos;
			FMath::SinCos(&Sin, &Cos, (63.0f / 64.0f) * 2.0f * PI);
			FVector2D LineStart(Sin * LoadingRange, Cos * LoadingRange);

			for (int32 i = 0; i < 64; i++)
			{
				FMath::SinCos(&Sin, &Cos, (i / 64.0f) * 2.0f * PI);
				FVector2D LineEnd(Sin * LoadingRange, Cos * LoadingRange);
				LineItem.Draw(CanvasObject, WorldToScreen(FVector2D(Source.Location) + LineStart), WorldToScreen(FVector2D(Source.Location) + LineEnd));
				LineStart = LineEnd;
			}

			FVector2D SourceDir = FVector2D(Source.Rotation.Vector());

			if (SourceDir.Size())
			{
				SourceDir.Normalize();
				FVector2D ConeCenter(FVector2D(Source.Location));
				LineItem.Draw(CanvasObject, WorldToScreen(ConeCenter), WorldToScreen(ConeCenter + SourceDir * LoadingRange));
			}
		}

		// Draw WorldPartition name
		FVector2D GridInfoPos = GridScreenOffset - GridScreenHalfExtent;
		{
			const FString GridInfoText = UWorld::RemovePIEPrefix(FPaths::GetBaseFilename(WorldPartition->GetPackage()->GetName()));
			float TextWidth, TextHeight;
			Canvas->StrLen(GEngine->GetTinyFont(), GridInfoText, TextWidth, TextHeight);
			Canvas->SetDrawColor(255, 255, 255);
			Canvas->DrawText(GEngine->GetTinyFont(), GridInfoText, GridInfoPos.X, GridInfoPos.Y);
			GridInfoPos.Y += TextHeight + 1;
		}

		// Draw Grid name, loading range
		{
			FString GridInfoText = FString::Printf(TEXT("%s | %d m"), *StreamingGrid.GridName.ToString(), int32(LoadingRange * 0.01f));
			Canvas->SetDrawColor(255, 255, 0);
			Canvas->DrawText(GEngine->GetTinyFont(), GridInfoText, GridInfoPos.X, GridInfoPos.Y);
		}

		FCanvasBoxItem Box(GridScreenBounds.Min, GridScreenBounds.GetSize());
		Box.SetColor(StreamingGrid.DebugColor);
		Canvas->DrawItem(Box);

		++GridIndex;
	}
}
