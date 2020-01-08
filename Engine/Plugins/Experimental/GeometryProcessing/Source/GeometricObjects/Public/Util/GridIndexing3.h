// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp GridIndexing3

#pragma once

#include "VectorTypes.h"


/**
 * Convert between integer grid coordinates and scaled real-valued coordinates (ie assumes integer grid origin == real origin)
 */
template<typename RealType>
struct TScaleGridIndexer3
{
	/** Real-valued size of an integer grid cell */
	RealType CellSize;

	TScaleGridIndexer3() : CellSize((RealType)1) 
	{
	}
	
	TScaleGridIndexer3(RealType CellSize) : CellSize(CellSize) 
	{
	}

	/** Convert real-valued point to integer grid coordinates */
	inline FVector3i ToGrid(const FVector3<RealType>& P) const
	{
		return FVector3i((int)(P.X / CellSize), (int)(P.Y / CellSize), (int)(P.Z / CellSize));
	}

	/** Convert integer grid coordinates to real-valued point */
	inline FVector3<RealType> FromGrid(const FVector3i& GridPoint) const
	{
		return FVector3<RealType>(GridPoint.X*CellSize, GridPoint.Y*CellSize, GridPoint.Z*CellSize);
	}
};
typedef TScaleGridIndexer3<float> FScaleGridIndexer3f;
typedef TScaleGridIndexer3<double> FScaleGridIndexer3d;



/**
 * Convert between integer grid coordinates and scaled+translated real-valued coordinates
 */
template<typename RealType>
struct TShiftGridIndexer3
{
	/** Real-valued size of an integer grid cell */
	RealType CellSize;
	/** Real-valued origin of grid, position of integer grid origin */
	FVector3<RealType> Origin;

	TShiftGridIndexer3() 
		: CellSize((RealType)1), Origin(FVector3<RealType>::Zero())
	{
	}

	TShiftGridIndexer3(const FVector3<RealType>& origin, RealType cellSize)
		: CellSize(cellSize), Origin(origin)
	{
	}

	/** Convert real-valued point to integer grid coordinates */
	inline FVector3i ToGrid(const FVector3<RealType>& point) const
	{
		return FVector3i(
			(int)((point.X - Origin.X) / CellSize),
			(int)((point.Y - Origin.Y) / CellSize),
			(int)((point.Z - Origin.Z) / CellSize));
	}

	/** Convert integer grid coordinates to real-valued point */
	inline FVector3<RealType> FromGrid(const FVector3i& gridpoint) const
	{
		return FVector3<RealType>(
			((RealType)gridpoint.X * CellSize) + Origin.X,
			((RealType)gridpoint.Y * CellSize) + Origin.Y,
			((RealType)gridpoint.Z * CellSize) + Origin.Z);
	}

	/** Convert real-valued grid coordinates to real-valued point */
	inline FVector3<RealType> FromGrid(const FVector3<RealType>& gridpointf)  const
	{
		return FVector3d(
			((RealType)gridpointf.X * CellSize) + Origin.X,
			((RealType)gridpointf.Y * CellSize) + Origin.Y,
			((RealType)gridpointf.Z * CellSize) + Origin.Z);
	}
};
typedef TShiftGridIndexer3<float> FShiftGridIndexer3f;
typedef TShiftGridIndexer3<double> FShiftGridIndexer3d;



