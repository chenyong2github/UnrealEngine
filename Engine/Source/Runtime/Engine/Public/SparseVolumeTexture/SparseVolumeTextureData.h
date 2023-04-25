// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SparseVolumeTexture/SparseVolumeTexture.h"

struct ENGINE_API FSparseVolumeTextureDataCreateInfo
{
	FIntVector3 VirtualVolumeAABBMin = FIntVector3(INT32_MAX, INT32_MAX, INT32_MAX);
	FIntVector3 VirtualVolumeAABBMax = FIntVector3(INT32_MIN, INT32_MIN, INT32_MIN);
	TStaticArray<EPixelFormat, 2> AttributesFormats = TStaticArray<EPixelFormat, 2>(InPlace, PF_Unknown); // Currently supported: PF_R8, PF_R8G8, PF_R8G8B8A8, PF_R16F, PF_G16R16F, PF_FloatRGBA, PF_R32_FLOAT, PF_G32R32F, PF_A32B32G32R32F
	TStaticArray<FVector4f, 2> FallbackValues = TStaticArray<FVector4f, 2>(InPlace, FVector4f());
};

class ENGINE_API ISparseVolumeTextureDataProvider
{
public:
	virtual FSparseVolumeTextureDataCreateInfo GetCreateInfo() const = 0;
	virtual void IteratePhysicalSource(TFunctionRef<void(const FIntVector3& Coord, int32 AttributesIdx, int32 ComponentIdx, float VoxelValue)> OnVisit) const = 0;
	virtual ~ISparseVolumeTextureDataProvider() = default;
};

struct ENGINE_API FSparseVolumeTextureDataAddressingInfo
{
	FIntVector3 VolumeResolution;
	TextureAddress AddressX;
	TextureAddress AddressY;
	TextureAddress AddressZ;
};

// Holds the data for a SparseVolumeTexture that is stored on disk. It only has a single mip after importing a source asset. The mip chain is built during cooking.
// Tiles are addressed by a flat index; unlike the runtime representation, this one stores all tiles in a 1D array (per mip level) and doesn't have the concept of a 3D physical tile texture.
// The page table itself is 3D though.
struct ENGINE_API FSparseVolumeTextureData
{
	struct FMipMap
	{
		TArray<uint32> PageTable;
		TArray64<uint8> PhysicalTileDataA;
		TArray64<uint8> PhysicalTileDataB;
		int32 NumPhysicalTiles;
	};

	static const uint32 kVersion = 0; // The current data format version for the raw source data.
	uint32 Version = kVersion; // This version can be used to convert existing source data to new version later.

	FSparseVolumeTextureHeader Header = {};
	TArray<FMipMap> MipMaps;

	void Serialize(FArchive& Ar);
	
	bool Create(const ISparseVolumeTextureDataProvider& DataProvider);
	bool CreateFromDense(const FSparseVolumeTextureDataCreateInfo& CreateInfo, const TArrayView<uint8>& VoxelDataA, const TArrayView<uint8>& VoxelDataB);
	void CreateDefault();
	FVector4f Load(const FIntVector3& VolumeCoord, int32 MipLevel, int32 AttributesIdx, const FSparseVolumeTextureDataAddressingInfo& AddressingInfo) const;
	bool BuildDerivedData(const FSparseVolumeTextureDataAddressingInfo& AddressingInfo, int32 NumMipLevels, bool bMoveMip0FromThis, FSparseVolumeTextureData& OutDerivedData);

private:

	bool GenerateMipMaps(const FSparseVolumeTextureDataAddressingInfo& AddressingInfo, int32 NumMipLevels = -1);
	bool GenerateBorderVoxels(const FSparseVolumeTextureDataAddressingInfo& AddressingInfo, int32 MipLevel, const TArray<FIntVector3>& PageCoords);
	bool DeduplicateTiles();

	uint32 ReadPageTable(const FIntVector3& PageTableCoord, int32 MipLevel) const;
	FVector4f ReadTileDataVoxel(int32 TileIndex, const FIntVector3& TileDataCoord, int32 MipLevel, int32 AttributesIdx) const;
	void WriteTileDataVoxel(int32 TileIndex, const FIntVector3& TileDataCoord, int32 MipLevel, int32 AttributesIdx, const FVector4f& Value, int32 DstComponent = -1);
};
