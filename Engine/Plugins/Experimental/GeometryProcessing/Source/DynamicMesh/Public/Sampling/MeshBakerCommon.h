// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Image/ImageTile.h"

namespace UE
{
namespace Geometry
{

/** Compute base/detail mesh correspondence by raycast */
bool GetDetailMeshTrianglePoint_Raycast(
	const FDynamicMesh3& DetailMesh,
	const FDynamicMeshAABBTree3& DetailSpatial,
	const FVector3d& BasePoint,
	const FVector3d& BaseNormal,
	int32& DetailTriangleOut,
	FVector3d& DetailTriBaryCoords,
	double Thickness,
	bool bFailToNearestPoint
);

/** Compute base/detail mesh correspondence by nearest distance */
bool GetDetailMeshTrianglePoint_Nearest(
	const FDynamicMesh3& DetailMesh,
	const FDynamicMeshAABBTree3& DetailSpatial,
	const FVector3d& BasePoint,
	int32& DetailTriangleOut,
	FVector3d& DetailTriBaryCoords);

/** Image tile storage for map bakes. */
class FMeshMapTileBuffer
{
public:
	FMeshMapTileBuffer(const FImageTile& TileIn, const int32 PixelSizeIn)
		: Tile(TileIn)
		, PixelSize(PixelSizeIn + 1) // + 1 for accumulated pixel weight.
	{
		Buffer = static_cast<float*>(FMemory::MallocZeroed(sizeof(float) * PixelSize * Tile.Num()));
	}

	~FMeshMapTileBuffer()
	{
		FMemory::Free(Buffer);
	}

	float& GetPixelWeight(const int64 LinearIdx) const
	{
		checkSlow(LinearIdx >= 0 && LinearIdx < Tile.Num());
		return Buffer[LinearIdx * PixelSize];
	}

	float* GetPixel(const int64 LinearIdx) const
	{
		checkSlow(LinearIdx >= 0 && LinearIdx < Tile.Num());
		return &Buffer[LinearIdx * PixelSize + 1];
	}

private:
	const FImageTile Tile;
	const int32 PixelSize;
	float* Buffer;
};

} // end namespace UE::Geometry
} // end namespace UE
	