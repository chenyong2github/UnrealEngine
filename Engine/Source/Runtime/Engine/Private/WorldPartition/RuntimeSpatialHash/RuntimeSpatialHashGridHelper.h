// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Algo/Find.h"
#include "Algo/Transform.h"

#include "ProfilingDebugging/ScopedTimers.h"

#include "WorldPartition/WorldPartitionRuntimeSpatialHash.h"
#include "WorldPartition/WorldPartitionActorCluster.h"
#include "WorldPartition/DataLayer/DataLayersID.h"

/**
  * Square 2D grid helper
  */
struct FSquare2DGridHelper
{
	struct FGrid2D
	{
		FVector2D Origin;
		int32 CellSize;
		int32 GridSize;

		inline FGrid2D(const FVector2D& InOrigin, int32 InCellSize, int32 InGridSize)
			: Origin(InOrigin)
			, CellSize(InCellSize)
			, GridSize(InGridSize)
		{}

		/**
		 * Validate that the coordinates fit the grid size
		 *
		 * @return true if the specified coordinates are valid
		 */
		inline bool IsValidCoords(const FIntVector2& InCoords) const
		{
			return (InCoords.X >= 0) && (InCoords.X < GridSize) && (InCoords.Y >= 0) && (InCoords.Y < GridSize);
		}

		/**
		 * Returns the cell bounds
		 *
		 * @return true if the specified index was valid
		 */
		inline bool GetCellBounds(int32 InIndex, FBox2D& OutBounds) const
		{
			if (InIndex >= 0 && InIndex <= (GridSize * GridSize))
			{
				const FIntVector2 Coords(InIndex % GridSize, InIndex / GridSize);
				return GetCellBounds(Coords, OutBounds);
			}

			return false;
		}

		/**
		 * Returns the cell bounds
		 *
		 * @return true if the specified coord was valid
		 */
		inline bool GetCellBounds(const FIntVector2& InCoords, FBox2D& OutBounds) const
		{
			if (IsValidCoords(InCoords))
			{
				const FVector2D Min = (FVector2D(Origin) - FVector2D(GridSize * CellSize * 0.5f, GridSize * CellSize * 0.5f)) + FVector2D(InCoords.X * CellSize, InCoords.Y * CellSize);
				const FVector2D Max = Min + FVector2D(CellSize, CellSize);
				OutBounds = FBox2D(Min, Max);
				return true;
			}

			return false;
		}

		/**
		 * Returns the cell coordinates of the provided position
		 *
		 * @return true if the position was inside the grid
		 */
		inline bool GetCellCoords(const FVector2D& InPos, FIntVector2& OutCoords) const
		{
			OutCoords = FIntVector2(
				FMath::FloorToInt(((InPos.X - Origin.X) / CellSize) + GridSize * 0.5f),
				FMath::FloorToInt(((InPos.Y - Origin.Y) / CellSize) + GridSize * 0.5f)
			);

			return IsValidCoords(OutCoords);
		}

		/**
		 * Returns the cells coordinates of the provided box
		 *
		 * @return true if the bounds was intersecting with the grid
		 */
		inline bool GetCellCoords(const FBox2D& InBounds2D, FIntVector2& OutMinCellCoords, FIntVector2& OutMaxCellCoords) const
		{
			GetCellCoords(InBounds2D.Min, OutMinCellCoords);
			if ((OutMinCellCoords.X >= GridSize) || (OutMinCellCoords.Y >= GridSize))
			{
				return false;
			}

			GetCellCoords(InBounds2D.Max, OutMaxCellCoords);
			if ((OutMaxCellCoords.X < 0) || (OutMaxCellCoords.Y < 0))
			{
				return false;
			}

			OutMinCellCoords.X = FMath::Clamp(OutMinCellCoords.X, 0, GridSize - 1);
			OutMinCellCoords.Y = FMath::Clamp(OutMinCellCoords.Y, 0, GridSize - 1);
			OutMaxCellCoords.X = FMath::Clamp(OutMaxCellCoords.X, 0, GridSize - 1);
			OutMaxCellCoords.Y = FMath::Clamp(OutMaxCellCoords.Y, 0, GridSize - 1);

			return true;
		}

		/**
		 * Returns the cell index of the provided coords
		 *
		 * @return true if the coords was inside the grid
		 */
		inline bool GetCellIndex(const FIntVector2& InCoords, uint32& OutIndex) const
		{
			if (IsValidCoords(InCoords))
			{
				OutIndex = (InCoords.Y * GridSize) + InCoords.X;
				return true;
			}

			return false;
		}

		/**
		 * Returns the cell index of the provided position
		 *
		 * @return true if the position was inside the grid
		 */
		inline bool GetCellIndex(const FVector& InPos, uint32& OutIndex) const
		{
			FIntVector2 Coords = FIntVector2(
				FMath::FloorToInt(((InPos.X - Origin.X) / CellSize) + GridSize * 0.5f),
				FMath::FloorToInt(((InPos.Y - Origin.Y) / CellSize) + GridSize * 0.5f)
			);

			return GetCellIndex(Coords, OutIndex);
		}

		/**
		 * Get the number of intersecting cells of the provided box
		 *
		 * @return the number of intersecting cells
		 */
		int32 GetNumIntersectingCells(const FBox& InBox) const
		{
			FIntVector2 MinCellCoords;
			FIntVector2 MaxCellCoords;
			const FBox2D Bounds2D(FVector2D(InBox.Min), FVector2D(InBox.Max));

			if (GetCellCoords(Bounds2D, MinCellCoords, MaxCellCoords))
			{
				return (MaxCellCoords.X - MinCellCoords.X + 1) * (MaxCellCoords.Y - MinCellCoords.Y + 1);
			}

			return 0;
		}

		// Runs a function on all cells
		void ForEachCells(TFunctionRef<void(const FIntVector2&)> InOperation) const
		{
			for (int32 y = 0; y < GridSize; y++)
			{
				for (int32 x = 0; x < GridSize; x++)
				{
					InOperation(FIntVector2(x, y));
				}
			}
		}

		/**
		 * Runs a function on all intersecting cells for the provided box
		 *
		 * @return the number of intersecting cells
		 */
		int32 ForEachIntersectingCellsBreakable(const FBox& InBox, TFunctionRef<bool(const FIntVector2&)> InOperation) const
		{
			int32 NumCells = 0;

			FIntVector2 MinCellCoords;
			FIntVector2 MaxCellCoords;
			const FBox2D Bounds2D(FVector2D(InBox.Min), FVector2D(InBox.Max));

			if (GetCellCoords(Bounds2D, MinCellCoords, MaxCellCoords))
			{
				for (int32 y = MinCellCoords.Y; y <= MaxCellCoords.Y; y++)
				{
					for (int32 x = MinCellCoords.X; x <= MaxCellCoords.X; x++)
					{
						if (!InOperation(FIntVector2(x, y)))
						{
							return NumCells;
						}
						++NumCells;
					}
				}
			}

			return NumCells;
		}

		int32 ForEachIntersectingCells(const FBox& InBox, TFunctionRef<void(const FIntVector2&)> InOperation) const
		{
			return ForEachIntersectingCellsBreakable(InBox, [InOperation](const FIntVector2& Vector) { InOperation(Vector); return true; });
		}

		/**
		 * Runs a function on all intersecting cells for the provided sphere
		 *
		 * @return the number of intersecting cells
		 */
		int32 ForEachIntersectingCells(const FSphere& InSphere, TFunctionRef<void(const FIntVector2&)> InOperation) const
		{
			int32 NumCells = 0;

			// @todo_ow: rasterize circle instead?
			const FBox Box(InSphere.Center - FVector(InSphere.W), InSphere.Center + FVector(InSphere.W));

			ForEachIntersectingCells(Box, [this, &InSphere, &InOperation, &NumCells](const FIntVector2& Coords)
				{
					const int32 CellIndex = Coords.Y * GridSize + Coords.X;

					FBox2D CellBounds;
					GetCellBounds(CellIndex, CellBounds);

					FVector2D Delta = FVector2D(InSphere.Center) - FVector2D::Max(CellBounds.GetCenter() - CellBounds.GetExtent(), FVector2D::Min(FVector2D(InSphere.Center), CellBounds.GetCenter() + CellBounds.GetExtent()));
					if ((Delta.X * Delta.X + Delta.Y * Delta.Y) < (InSphere.W * InSphere.W))
					{
						InOperation(Coords);
						NumCells++;
					}
				});

			return NumCells;
		}
	};

	struct FGridLevel : public FGrid2D
	{
#if WITH_EDITOR
		struct FGridCellDataChunk
		{
			FGridCellDataChunk(const TArray<const UDataLayer*>& InDataLayers)
			{
				Algo::TransformIf(InDataLayers, DataLayers, [](const UDataLayer* DataLayer) { return DataLayer->IsDynamicallyLoaded(); }, [](const UDataLayer* DataLayer) { return DataLayer; });
				DataLayersID = FDataLayersID(DataLayers);
			}

			void AddActor(FActorInstance ActorInstance) { Actors.Add(MoveTemp(ActorInstance)); }
			const TSet<FActorInstance>& GetActors() const { return Actors; }
			bool HasDataLayers() const { return !DataLayers.IsEmpty(); }
			const TArray<const UDataLayer*>& GetDataLayers() const { return DataLayers; }
			const FDataLayersID& GetDataLayersID() const { return DataLayersID; }

		private:
			TSet<FActorInstance> Actors;
			TArray<const UDataLayer*> DataLayers;
			FDataLayersID DataLayersID;
		};

		struct FGridCell
		{
			FGridCell(const FIntVector& InCoords)
				: Coords(InCoords)
			{}

			void AddActor(FActorInstance ActorInstance, const TArray<const UDataLayer*>& InDataLayers)
			{
				FDataLayersID DataLayersID = FDataLayersID(InDataLayers);
				FGridCellDataChunk* ActorDataChunk = Algo::FindByPredicate(DataChunks, [&](FGridCellDataChunk& InDataChunk) { return InDataChunk.GetDataLayersID() == DataLayersID; });
				if (!ActorDataChunk)
				{
					ActorDataChunk = &DataChunks.Emplace_GetRef(InDataLayers);
				}
				ActorDataChunk->AddActor(MoveTemp(ActorInstance));
			}

			void AddActors(const TSet<FGuid>& InActors, const FActorContainerInstance* ContainerInstance, const TArray<const UDataLayer*>& InDataLayers)
			{
				for (const FGuid& Actor : InActors)
				{
					AddActor(FActorInstance(Actor, ContainerInstance), InDataLayers);
				}
			}

			const TArray<FGridCellDataChunk>& GetDataChunks() const { return DataChunks; }

			const FGridCellDataChunk* GetNoDataLayersDataChunk() const
			{
				for (const FGridCellDataChunk& DataChunk : DataChunks)
				{
					if (!DataChunk.HasDataLayers())
					{
						return &DataChunk;
					}
				}
				return nullptr;
			}

			FIntVector GetCoords() const
			{
				return Coords;
			}

		private:
			FIntVector Coords;
			TArray<FGridCellDataChunk> DataChunks;
		};
#endif

		inline FGridLevel(const FVector2D& InOrigin, int32 InCellSize, int32 InGridSize, int32 InLevel)
			: FGrid2D(InOrigin, InCellSize, InGridSize)
#if WITH_EDITOR
			, Level(InLevel)
#endif
		{}

#if WITH_EDITOR
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

			int32 CellIndexMapping;
			int32* CellIndexMappingPtr = CellsMapping.Find(CellIndex);
			if (CellIndexMappingPtr)
			{
				CellIndexMapping = *CellIndexMappingPtr;
			}
			else
			{
				CellIndexMapping = Cells.Emplace(FIntVector(InCoords.X, InCoords.Y, Level));
				CellsMapping.Add(CellIndex, CellIndexMapping);
			}

			return Cells[CellIndexMapping];
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

			int32 CellIndexMapping = CellsMapping.FindChecked(CellIndex);

			const FGridCell& Cell = Cells[CellIndexMapping];
			check(Cell.GetCoords() == FIntVector(InCoords.X, InCoords.Y, Level));
			return Cell;
		}

		int32 Level;
		TArray<FGridCell> Cells;
		TMap<int32, int32> CellsMapping;
#endif
	};

	FSquare2DGridHelper(const FBox& InWorldBounds, const FVector& InOrigin, int32 InCellSize);

#if WITH_EDITOR
	// Returns the lowest grid level
	inline FGridLevel& GetLowestLevel() { return Levels[0]; }

	// Returns the always loaded (top level) cell
	inline FGridLevel::FGridCell& GetAlwaysLoadedCell() { return Levels.Last().GetCell(FIntVector2(0,0)); }

	// Returns the always loaded (top level) cell
	inline const FGridLevel::FGridCell& GetAlwaysLoadedCell() const { return Levels.Last().GetCell(FIntVector2(0,0)); }

	// Returns the cell at the given coord
	inline const FGridLevel::FGridCell& GetCell(const FIntVector& InCoords) const { return Levels[InCoords.Z].GetCell(FIntVector2(InCoords.X, InCoords.Y)); }
#endif

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
	 * Returns the cell global coordinates
	 *
	 * @return true if the specified coord was valid
	 */
	inline bool GetCellGlobalCoords(const FIntVector& InCoords, FIntVector& OutGlobalCoords) const
	{
		if (Levels.IsValidIndex(InCoords.Z))
		{
			const FGridLevel& GridLevel = Levels[InCoords.Z];
			if (GridLevel.IsValidCoords(FIntVector2(InCoords.X, InCoords.Y)))
			{
				int32 CoordOffset = Levels[InCoords.Z].GridSize >> 1;
				OutGlobalCoords = InCoords;
				OutGlobalCoords.X -= CoordOffset;
				OutGlobalCoords.Y -= CoordOffset;
				return true;
			}
		}
		return false;
	}

#if WITH_EDITOR
	// Runs a function on all cells
	void ForEachCells(TFunctionRef<void(const FSquare2DGridHelper::FGridLevel::FGridCell&)> InOperation) const;
#endif

	/**
	 * Runs a function on all intersecting cells for the provided box
	 *
	 * @return the number of intersecting cells
	 */
	int32 ForEachIntersectingCells(const FBox& InBox, TFunctionRef<void(const FIntVector&)> InOperation) const;

	/**
	 * Runs a function on all intersecting cells for the provided sphere
	 *
	 * @return the number of intersecting cells
	 */
	int32 ForEachIntersectingCells(const FSphere& InSphere, TFunctionRef<void(const FIntVector&)> InOperation) const;

#if WITH_EDITOR
	// Validates that actor is not referenced by multiple cells
	void ValidateSingleActorReferer();
#endif

public:
	FBox WorldBounds;
	FVector Origin;
	int32 CellSize;
	TArray<FGridLevel> Levels;
};

#if WITH_EDITOR
FSquare2DGridHelper GetGridHelper(const FBox& WorldBounds, int32 GridCellSize);
FSquare2DGridHelper GetPartitionedActors(const UWorldPartition* WorldPartition, const FBox& WorldBounds, const FSpatialHashRuntimeGrid& Grid, const TArray<const FActorClusterInstance*>& GridActors);
#endif // #if WITH_EDITOR
