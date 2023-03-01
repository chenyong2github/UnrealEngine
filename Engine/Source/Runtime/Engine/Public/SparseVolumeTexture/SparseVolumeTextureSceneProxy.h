// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SparseVolumeTexture/SparseVolumeTexture.h"

struct FSparseVolumeTextureData;

struct FSparseVolumeTextureTileMapping
{
	const uint32* TileIndices; // Indices to tiles stored in TileDataA and TileDataB
	const uint8* TileDataA;
	const uint8* TileDataB;
	int32 NumPhysicalTiles; // Number of tiles stored in TileDataA and TileDataB
	int32 TileIndicesOffset; // Offset to apply to each element in TileIndices
};

struct FSparseVolumeTextureRuntimeHeader : public FSparseVolumeTextureHeader
{
	FIntVector3 TileDataVolumeResolution = FIntVector3::ZeroValue;
	int32 NumMipLevels = 0;
	int32 HighestResidentLevel = INT32_MIN;
	int32 LowestResidentLevel = INT32_MAX;
};

// The structure represent the runtime data cooked runtime data.
struct FSparseVolumeTextureRuntime
{
	FSparseVolumeTextureRuntimeHeader Header;
	TArray<TArray<uint32>> PageTable;
	TArray<uint8> PhysicalTileDataA;
	TArray<uint8> PhysicalTileDataB;

	bool Create(const FSparseVolumeTextureData& TextureData);
	bool Create(const FSparseVolumeTextureHeader& SVTHeader, int32 NumMipLevels);
	/* Sets the mapped physical tile data for this SVT. The array must have one entry per mip level. */
	bool SetTileMappings(const TArrayView<const FSparseVolumeTextureTileMapping>& Mappings);
	void SetAsDefaultTexture();
};

class FSparseVolumeTextureSceneProxy : public FRenderResource
{
public:

	FSparseVolumeTextureSceneProxy();
	virtual ~FSparseVolumeTextureSceneProxy() override;

	FSparseVolumeTextureRuntime& GetRuntimeData() { return SparseVolumeTextureRuntime; }
	const FSparseVolumeTextureRuntime& GetRuntimeData() const { return SparseVolumeTextureRuntime; }
	const FSparseVolumeTextureRuntimeHeader& GetHeader() const { return SparseVolumeTextureRuntime.Header; }
	FTextureRHIRef GetPhysicalTileDataATextureRHI() const { return PhysicalTileDataATextureRHI; }
	FTextureRHIRef GetPhysicalTileDataBTextureRHI() const { return PhysicalTileDataBTextureRHI; }
	FTextureRHIRef GetPageTableTextureRHI() const { return PageTableTextureRHI; }
	void GetMemorySize(SIZE_T* SizeCPU, SIZE_T* SizeGPU) const;

	virtual void InitRHI() override;
	virtual void ReleaseRHI() override;

private:

	FSparseVolumeTextureRuntime			SparseVolumeTextureRuntime;

	FTextureRHIRef						PageTableTextureRHI;
	FTextureRHIRef						PhysicalTileDataATextureRHI;
	FTextureRHIRef						PhysicalTileDataBTextureRHI;
};
