// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Math/Box2D.h"
#include "Math/IntVector.h"
#include "Math/Vector2D.h"
#include "Templates/Function.h"

/** 
  * Square 2D grid helper
  */
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

	/** 
	 * Runs a function on all intersecting cells for the provided box
	 *
	 * @return the number of intersecting cells
	 */
	int32 ForEachIntersectingCells(const FBox& InBox, TFunctionRef<void(const FIntVector2&)> InOperation) const
	{
		int32 NumCells = 0;

		FIntVector2 MinCellCoords;
		FIntVector2 MaxCellCoords;
		const FBox2D Bounds2D(FVector2D(InBox.Min), FVector2D(InBox.Max));

		if (GetCellCoords(Bounds2D, MinCellCoords, MaxCellCoords))
		{
			for (int32 y=MinCellCoords.Y; y<=MaxCellCoords.Y; y++)
			{
				for (int32 x=MinCellCoords.X; x<=MaxCellCoords.X; x++)
				{
					InOperation(FIntVector2(x, y));
					NumCells++;
				}
			}
		}

		return NumCells;
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
