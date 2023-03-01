// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SparseVolumeTexture/SparseVolumeTexture.h"

class ENGINE_API ISparseVolumeTextureDataConstructionAdapter
{
public:
	struct ENGINE_API FAttributesInfo
	{
		EPixelFormat Format;
		FVector4f FallbackValue;
		FVector4f NormalizeScale;
		FVector4f NormalizeBias;
		bool bNormalized;
	};

	virtual void GetAttributesInfo(FAttributesInfo& OutInfoA, FAttributesInfo& OutInfoB) const = 0;
	virtual FIntVector3 GetAABBMin() const = 0;
	virtual FIntVector3 GetAABBMax() const = 0;
	virtual FIntVector3 GetResolution() const = 0;
	virtual void IteratePhysicalSource(TFunctionRef<void(const FIntVector3& Coord, int32 AttributesIdx, int32 ComponentIdx, float VoxelValue)> OnVisit) const = 0;
	virtual ~ISparseVolumeTextureDataConstructionAdapter() = default;
};

struct ENGINE_API FSparseVolumeTextureMipInfo
{
	int32 TileOffset = 0;
	int32 TileCount = 0;
};

struct ENGINE_API FSparseVolumeTextureDataHeader : public FSparseVolumeTextureHeader
{
	static const uint32 kVersion = 0; // The current data format version for the header.
	uint32 Version = kVersion; // This version can be used to convert existing header to new version later.
	TArray<FSparseVolumeTextureMipInfo> MipInfo;

	void Serialize(FArchive& Ar);
};

// Holds the data for a SparseVolumeTexture that is stored on disk. It only has a single mip after importing a source asset. The mip chain is built during cooking.
// Tiles are addressed by a flat index; unlike the runtime representation, this one stores all tiles in a 1D array and doesn't have the concept of a 3D physical tile texture.
// The page table itself is 3D though.
struct ENGINE_API FSparseVolumeTextureData
{
	static const uint32 kVersion = 0; // The current data format version for the raw source data.
	uint32 Version = kVersion; // This version can be used to convert existing source data to new version later.

	FSparseVolumeTextureDataHeader Header = {};
	TArray<TArray<uint32>> PageTable; // PageTable[MipLevel][PageCoord]
	TArray<uint8> PhysicalTileDataA;
	TArray<uint8> PhysicalTileDataB;

	void Serialize(FArchive& Ar);
	bool Construct(const ISparseVolumeTextureDataConstructionAdapter& Adapter);
	uint32 ReadPageTable(const FIntVector3& PageTableCoord, int32 MipLevel) const;
	FVector4f ReadTileDataVoxel(int32 TileIndex, const FIntVector3& TileDataCoord, int32 AttributesIdx) const;
	FVector4f Load(const FIntVector3& VolumeCoord, int32 MipLevel, int32 AttributesIdx) const;
	void WriteTileDataVoxel(int32 TileIndex, const FIntVector3& TileDataCoord, int32 AttributesIdx, const FVector4f& Value, int32 DstComponent = -1);
	void GenerateMipMaps(int32 NumMipLevels = -1);
};
