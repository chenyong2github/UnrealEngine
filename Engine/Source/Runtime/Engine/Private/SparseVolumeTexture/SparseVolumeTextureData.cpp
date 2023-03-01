// Copyright Epic Games, Inc. All Rights Reserved.

#include "SparseVolumeTexture/SparseVolumeTextureData.h"
#include "SparseVolumeTexture/SparseVolumeTextureUtility.h"

DEFINE_LOG_CATEGORY_STATIC(LogSparseVolumeTextureData, Log, All);

////////////////////////////////////////////////////////////////////////////////////////////////

void FSparseVolumeTextureDataHeader::Serialize(FArchive& Ar)
{
	FSparseVolumeTextureHeader::Serialize(Ar);

	Ar << Version;

	if (Version == 0)
	{
		int32 NumMipLevels = MipInfo.Num();
		Ar << NumMipLevels;
		if (Ar.IsLoading())
		{
			MipInfo.Reset(NumMipLevels);
			MipInfo.SetNum(NumMipLevels);
		}
		for (int32 MipLevel = 0; MipLevel < NumMipLevels; ++MipLevel)
		{
			Ar << MipInfo[MipLevel].TileOffset;
			Ar << MipInfo[MipLevel].TileCount;
		}
	}
	else
	{
		// FSparseVolumeTextureDataHeader needs to account for new version
		check(false);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////

void FSparseVolumeTextureData::Serialize(FArchive& Ar)
{
	Header.Serialize(Ar);

	Ar << Version;

	if (Version == 0)
	{
		Ar << PageTable;
		Ar << PhysicalTileDataA;
		Ar << PhysicalTileDataB;
	}
	else
	{
		// FSparseVolumeTextureData needs to account for new version
		check(false);
	}
}

bool FSparseVolumeTextureData::Construct(const ISparseVolumeTextureDataConstructionAdapter& Adapter)
{
	using namespace UE::SVT;
	using namespace UE::SVT::Private;

	ISparseVolumeTextureDataConstructionAdapter::FAttributesInfo AttributesInfo[2];
	Adapter.GetAttributesInfo(AttributesInfo[0], AttributesInfo[1]);

	Header.MipInfo.SetNum(1);
	FSparseVolumeTextureMipInfo& MipInfo = Header.MipInfo[0];
	Header.AttributesFormats[0] = AttributesInfo[0].Format;
	Header.AttributesFormats[1] = AttributesInfo[1].Format;

	Header.VirtualVolumeResolution = Adapter.GetResolution();
	Header.VirtualVolumeAABBMin = Adapter.GetAABBMin();
	Header.VirtualVolumeAABBMax = Adapter.GetAABBMax();
	Header.PageTableVolumeAABBMin = Header.VirtualVolumeAABBMin / SPARSE_VOLUME_TILE_RES;
	Header.PageTableVolumeAABBMax = (Header.VirtualVolumeAABBMax + FIntVector3(SPARSE_VOLUME_TILE_RES - 1)) / SPARSE_VOLUME_TILE_RES;

	FIntVector3 PageTableVolumeResolution = Header.PageTableVolumeAABBMax - Header.PageTableVolumeAABBMin;
	
	// We need to ensure a power of two resolution for the page table in order to fit all mips of the page table into the physical mips of the texture resource.
	PageTableVolumeResolution.X = FMath::RoundUpToPowerOfTwo(PageTableVolumeResolution.X);
	PageTableVolumeResolution.Y = FMath::RoundUpToPowerOfTwo(PageTableVolumeResolution.Y);
	PageTableVolumeResolution.Z = FMath::RoundUpToPowerOfTwo(PageTableVolumeResolution.Z);

	Header.PageTableVolumeAABBMax = Header.PageTableVolumeAABBMin + PageTableVolumeResolution;

	if (PageTableVolumeResolution.X > SVTMaxVolumeTextureDim
		|| PageTableVolumeResolution.Y > SVTMaxVolumeTextureDim
		|| PageTableVolumeResolution.Z > SVTMaxVolumeTextureDim)
	{
		UE_LOG(LogSparseVolumeTextureData, Warning, TEXT("SparseVolumeTexture page table texture dimensions exceed limit (%ix%ix%i): %ix%ix%i"),
			SVTMaxVolumeTextureDim, SVTMaxVolumeTextureDim, SVTMaxVolumeTextureDim,
			PageTableVolumeResolution.X, PageTableVolumeResolution.Y, PageTableVolumeResolution.Z);
		return false;
	}

	Header.PageTableVolumeResolution = PageTableVolumeResolution;

	// Tag all pages with valid data
	TBitArray PagesWithData(false, Header.PageTableVolumeResolution.Z * Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.X);
	Adapter.IteratePhysicalSource([&](const FIntVector3& Coord, int32 AttributesIdx, int32 ComponentIdx, float VoxelValue)
		{
			const bool bIsFallbackValue = (VoxelValue == AttributesInfo[AttributesIdx].FallbackValue[ComponentIdx]);
			if (!bIsFallbackValue)
			{
				const FIntVector3 GridCoord = Coord;
				check(GridCoord.X >= 0 && GridCoord.Y >= 0 && GridCoord.Z >= 0);
				check(GridCoord.X < Header.VirtualVolumeAABBMax.X&& GridCoord.Y < Header.VirtualVolumeAABBMax.Y&& GridCoord.Z < Header.VirtualVolumeAABBMax.Z);
				const FIntVector3 PageCoord = (GridCoord / SPARSE_VOLUME_TILE_RES) - Header.PageTableVolumeAABBMin;
				check(PageCoord.X >= 0 && PageCoord.Y >= 0 && PageCoord.Z >= 0);
				check(PageCoord.X < Header.PageTableVolumeResolution.X&& PageCoord.Y < Header.PageTableVolumeResolution.Y&& PageCoord.Z < Header.PageTableVolumeResolution.Z);

				const int32 PageIndex = PageCoord.Z * (Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.X) + PageCoord.Y * Header.PageTableVolumeResolution.X + PageCoord.X;
				PagesWithData[PageIndex] = true;
			}
		});

	// Allocate some memory for temp data (worst case)
	TArray<FIntVector3> LinearAllocatedPages;
	LinearAllocatedPages.SetNum(Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.Z);

	// Go over each potential page from the source data and push allocate it if it has any data.
	// Otherwise point to the default empty page.
	int32 NumAllocatedPages = 0;
	for (int32_t PageZ = 0; PageZ < Header.PageTableVolumeResolution.Z; ++PageZ)
	{
		for (int32_t PageY = 0; PageY < Header.PageTableVolumeResolution.Y; ++PageY)
		{
			for (int32_t PageX = 0; PageX < Header.PageTableVolumeResolution.X; ++PageX)
			{
				const int32 PageIndex = PageZ * (Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.X) + PageY * Header.PageTableVolumeResolution.X + PageX;
				const bool bHasAnyData = PagesWithData[PageIndex];
				if (bHasAnyData)
				{
					LinearAllocatedPages[NumAllocatedPages] = FIntVector3(PageX, PageY, PageZ);
					NumAllocatedPages++;
				}
			}
		}
	}

	const int32 EffectivelyAllocatedTiles = NumAllocatedPages + 1 /*null tile*/;

	// Initialize the SparseVolumeTexture page and tile.
	const int32 FormatSize[] = { GPixelFormats[Header.AttributesFormats[0]].BlockBytes, GPixelFormats[Header.AttributesFormats[1]].BlockBytes };
	PageTable.SetNum(1);
	PageTable[0].SetNumZeroed(Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.Z);
	PhysicalTileDataA.SetNumZeroed(EffectivelyAllocatedTiles * SVTNumVoxelsPerTile * FormatSize[0]);
	PhysicalTileDataB.SetNumZeroed(EffectivelyAllocatedTiles * SVTNumVoxelsPerTile * FormatSize[1]);

	// Generate page table and tile volume data by splatting the data
	{
		uint32 DstTileIndex = 0;

		// Add an empty tile, reserve slot at coord 0
		{
			// Fill null tile with fallback/background value
			FVector4f FallbackValues[] = { AttributesInfo[0].FallbackValue, AttributesInfo[1].FallbackValue };
			for (int32 AttributesIdx = 0; AttributesIdx < 2; ++AttributesIdx)
			{
				if (AttributesInfo[AttributesIdx].bNormalized)
				{
					FallbackValues[AttributesIdx] = FallbackValues[AttributesIdx] * AttributesInfo[AttributesIdx].NormalizeScale + AttributesInfo[AttributesIdx].NormalizeBias;
				}
			}
			for (int32 Z = 0; Z < SPARSE_VOLUME_TILE_RES; ++Z)
			{
				for (int32 Y = 0; Y < SPARSE_VOLUME_TILE_RES; ++Y)
				{
					for (int32 X = 0; X < SPARSE_VOLUME_TILE_RES; ++X)
					{
						const FIntVector3 WriteCoord = FIntVector3(X, Y, Z);
						WriteTileDataVoxel(0 /*TileIndex*/, WriteCoord, 0 /*AttributesIndex*/, FallbackValues[0]);
						WriteTileDataVoxel(0 /*TileIndex*/, WriteCoord, 1 /*AttributesIndex*/, FallbackValues[1]);
					}
				}
			}

			Header.NullTileValues[0] = FallbackValues[0];
			Header.NullTileValues[1] = FallbackValues[1];

			// PageTable is all cleared to zero, simply skip a tile
			++DstTileIndex;
		}

		for (int32 i = 0; i < NumAllocatedPages; ++i)
		{
			const FIntVector3 PageCoordToSplat = LinearAllocatedPages[i];

			// Setup the page table entry
			PageTable[0]
				[
					PageCoordToSplat.Z * Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y +
					PageCoordToSplat.Y * Header.PageTableVolumeResolution.X +
					PageCoordToSplat.X
				] = DstTileIndex;

			// Set the next tile to be written to
			++DstTileIndex;
		}
		MipInfo.TileOffset = 0;
		MipInfo.TileCount = EffectivelyAllocatedTiles;
	}

	// Write physical tile data
	Adapter.IteratePhysicalSource([&](const FIntVector3& Coord, int32 AttributesIdx, int32 ComponentIdx, float VoxelValue)
		{
			const FIntVector3 GridCoord = Coord;
			check(GridCoord.X >= 0 && GridCoord.Y >= 0 && GridCoord.Z >= 0);
			check(GridCoord.X < Header.VirtualVolumeAABBMax.X&& GridCoord.Y < Header.VirtualVolumeAABBMax.Y&& GridCoord.Z < Header.VirtualVolumeAABBMax.Z);
			const FIntVector3 PageCoord = (GridCoord / SPARSE_VOLUME_TILE_RES) - Header.PageTableVolumeAABBMin;
			check(PageCoord.X >= 0 && PageCoord.Y >= 0 && PageCoord.Z >= 0);
			check(PageCoord.X < Header.PageTableVolumeResolution.X&& PageCoord.Y < Header.PageTableVolumeResolution.Y&& PageCoord.Z < Header.PageTableVolumeResolution.Z);

			const int32 PageIndex = PageCoord.Z * (Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.X) + PageCoord.Y * Header.PageTableVolumeResolution.X + PageCoord.X;

			if (!PagesWithData[PageIndex])
			{
				return;
			}

			float WriteValue = VoxelValue;
			if (AttributesInfo[AttributesIdx].bNormalized)
			{
				WriteValue = WriteValue * AttributesInfo[AttributesIdx].NormalizeScale[ComponentIdx] + AttributesInfo[AttributesIdx].NormalizeBias[ComponentIdx];
			}

			const int32 TileIndex = (int32)PageTable[0][PageIndex];
			const FIntVector3 TileLocalCoord = GridCoord % SPARSE_VOLUME_TILE_RES;
			WriteTileDataVoxel(TileIndex, TileLocalCoord, AttributesIdx, FVector4f(WriteValue, WriteValue, WriteValue, WriteValue), ComponentIdx);
		});

	return true;
}

uint32 FSparseVolumeTextureData::ReadPageTable(const FIntVector3& PageTableCoord, int32 MipLevel) const
{
	if (!Header.MipInfo.IsValidIndex(MipLevel))
	{
		return INDEX_NONE;
	}
	const int32 MipLevelFactor = 1 << MipLevel;
	const FIntVector3 PageTableResolution = FIntVector3(
		FMath::Max(1, Header.PageTableVolumeResolution.X / MipLevelFactor), 
		FMath::Max(1, Header.PageTableVolumeResolution.Y / MipLevelFactor), 
		FMath::Max(1, Header.PageTableVolumeResolution.Z / MipLevelFactor));
	if (PageTableCoord.X < 0 || PageTableCoord.Y < 0 || PageTableCoord.Z < 0
		|| PageTableCoord.X >= PageTableResolution.X
		|| PageTableCoord.Y >= PageTableResolution.Y
		|| PageTableCoord.Z >= PageTableResolution.Z)
	{
		return INDEX_NONE;
	}

	const int32 PageIndex = PageTableCoord.Z * (PageTableResolution.Y * PageTableResolution.X) + PageTableCoord.Y * PageTableResolution.X + PageTableCoord.X;
	if (PageTable[MipLevel].IsValidIndex(PageIndex))
	{
		return PageTable[MipLevel][PageIndex];
	}
	return INDEX_NONE;
}

FVector4f FSparseVolumeTextureData::ReadTileDataVoxel(int32 TileIndex, const FIntVector3& TileDataCoord, int32 AttributesIdx) const
{
	check(AttributesIdx >= 0 && AttributesIdx <= 1);
	if ((AttributesIdx == 0 && PhysicalTileDataA.IsEmpty()) || (AttributesIdx == 1 && PhysicalTileDataB.IsEmpty()))
	{
		return FVector4f();
	}

	// SV_TODO check for TileIndex oob

	if (TileDataCoord.X < 0 || TileDataCoord.Y < 0 || TileDataCoord.Z < 0
		|| TileDataCoord.X >= SPARSE_VOLUME_TILE_RES
		|| TileDataCoord.Y >= SPARSE_VOLUME_TILE_RES
		|| TileDataCoord.Z >= SPARSE_VOLUME_TILE_RES)
	{
		return FVector4f();
	}

	const int32 VoxelIndex = TileIndex * UE::SVT::SVTNumVoxelsPerTile + TileDataCoord.Z * (SPARSE_VOLUME_TILE_RES * SPARSE_VOLUME_TILE_RES) + TileDataCoord.Y * SPARSE_VOLUME_TILE_RES + TileDataCoord.X;
	const uint8* TileData = AttributesIdx == 0 ? PhysicalTileDataA.GetData() : PhysicalTileDataB.GetData();
	const EPixelFormat Format = Header.AttributesFormats[AttributesIdx];
	return UE::SVT::ReadVoxel(VoxelIndex, TileData, Format);
}

FVector4f FSparseVolumeTextureData::Load(const FIntVector3& VolumeCoord, int32 MipLevel, int32 AttributesIdx) const
{
	if (!Header.MipInfo.IsValidIndex(MipLevel))
	{
		return FVector4f();
	}
	const FIntVector3 PageTableCoord = (VolumeCoord / SPARSE_VOLUME_TILE_RES) - (Header.PageTableVolumeAABBMin / (1 << MipLevel));
	const uint32 TileIndex = ReadPageTable(PageTableCoord, MipLevel);
	const FIntVector3 VoxelCoord = VolumeCoord % SPARSE_VOLUME_TILE_RES;
	const FVector4f Sample = ReadTileDataVoxel(TileIndex, VoxelCoord, AttributesIdx);
	return Sample;
}

void FSparseVolumeTextureData::WriteTileDataVoxel(int32 TileIndex, const FIntVector3& TileDataCoord, int32 AttributesIdx, const FVector4f& Value, int32 DstComponent)
{
	check(AttributesIdx >= 0 && AttributesIdx <= 1);
	if ((AttributesIdx == 0 && PhysicalTileDataA.IsEmpty()) || (AttributesIdx == 1 && PhysicalTileDataB.IsEmpty()))
	{
		return;
	}

	// SV_TODO check for TileIndex oob

	if (TileDataCoord.X < 0 || TileDataCoord.Y < 0 || TileDataCoord.Z < 0
		|| TileDataCoord.X >= SPARSE_VOLUME_TILE_RES
		|| TileDataCoord.Y >= SPARSE_VOLUME_TILE_RES
		|| TileDataCoord.Z >= SPARSE_VOLUME_TILE_RES)
	{
		return;
	}

	const int32 VoxelIndex = TileIndex * UE::SVT::SVTNumVoxelsPerTile + TileDataCoord.Z * (SPARSE_VOLUME_TILE_RES * SPARSE_VOLUME_TILE_RES) + TileDataCoord.Y * SPARSE_VOLUME_TILE_RES + TileDataCoord.X;
	uint8* TileData = AttributesIdx == 0 ? PhysicalTileDataA.GetData() : PhysicalTileDataB.GetData();
	const EPixelFormat Format = Header.AttributesFormats[AttributesIdx];
	UE::SVT::WriteVoxel(VoxelIndex, TileData, Format, Value, DstComponent);
}

void FSparseVolumeTextureData::GenerateMipMaps(int32 NumMipLevels)
{
	check(!Header.MipInfo.IsEmpty());
	check(FMath::IsPowerOfTwo(Header.PageTableVolumeResolution.X) 
		&& FMath::IsPowerOfTwo(Header.PageTableVolumeResolution.Y) 
		&& FMath::IsPowerOfTwo(Header.PageTableVolumeResolution.Z));
	if (NumMipLevels == -1)
	{
		NumMipLevels = 1;
		FIntVector3 Resolution = Header.VirtualVolumeResolution;
		while (Resolution.X > SPARSE_VOLUME_TILE_RES || Resolution.Y > SPARSE_VOLUME_TILE_RES || Resolution.Z > SPARSE_VOLUME_TILE_RES)
		{
			Resolution /= 2;
			++NumMipLevels;
		}
	}
	Header.MipInfo.SetNum(1);
	PageTable.SetNum(1);

	for (int32 MipLevel = 1; MipLevel < NumMipLevels; ++MipLevel)
	{
		Header.MipInfo.AddDefaulted();
		FSparseVolumeTextureMipInfo& MipInfo = Header.MipInfo[MipLevel];
		MipInfo.TileOffset = Header.MipInfo[MipLevel - 1].TileOffset + Header.MipInfo[MipLevel - 1].TileCount;

		const int32 MipLevelFactor = 1 << MipLevel;
		const int32 ParentMipLevelFactor = 1 << (MipLevel - 1);
		const FIntVector3 PageTableVolumeAABBMin = (Header.VirtualVolumeAABBMin / SPARSE_VOLUME_TILE_RES) / MipLevelFactor;
		const FIntVector3 ParentPageTableVolumeAABBMin = (Header.VirtualVolumeAABBMin / SPARSE_VOLUME_TILE_RES) / ParentMipLevelFactor;
		const FIntVector3 PageTableVolumeResolution = FIntVector3(FMath::Max(1, Header.PageTableVolumeResolution.X / MipLevelFactor), FMath::Max(1, Header.PageTableVolumeResolution.Y / MipLevelFactor), FMath::Max(1, Header.PageTableVolumeResolution.Z / MipLevelFactor));
		
		check(FMath::IsPowerOfTwo(PageTableVolumeResolution.X)
			&& FMath::IsPowerOfTwo(PageTableVolumeResolution.Y)
			&& FMath::IsPowerOfTwo(PageTableVolumeResolution.Z));

		// Allocate some memory for temp data (worst case)
		TArray<FIntVector3> LinearAllocatedPages;
		LinearAllocatedPages.SetNum(PageTableVolumeResolution.X * PageTableVolumeResolution.Y * PageTableVolumeResolution.Z);

		// Go over each potential page from the source data and push allocate it if it has any data.
		// Otherwise point to the default empty page.
		int32 NumAllocatedPages = 0;
		for (int32_t PageZ = 0; PageZ < PageTableVolumeResolution.Z; ++PageZ)
		{
			for (int32_t PageY = 0; PageY < PageTableVolumeResolution.Y; ++PageY)
			{
				for (int32_t PageX = 0; PageX < PageTableVolumeResolution.X; ++PageX)
				{
					const FIntVector3 PageCoord = FIntVector3(PageX, PageY, PageZ);
					bool bHasAnyData = false;
					for (int32_t OffsetIdx = 0; OffsetIdx < 8; ++OffsetIdx)
					{
						const FIntVector3 Offset = FIntVector3(OffsetIdx, OffsetIdx >> 1, OffsetIdx >> 2) & 1;
						const FIntVector3 ParentPageTableCoord = (PageTableVolumeAABBMin + PageCoord) * 2 + Offset - ParentPageTableVolumeAABBMin;
						const uint32 PageSample = ReadPageTable(ParentPageTableCoord, MipLevel - 1);

						if (PageSample != INDEX_NONE && PageSample != 0)
						{
							bHasAnyData = true;
						}
					}
					if (bHasAnyData)
					{
						LinearAllocatedPages[NumAllocatedPages] = PageCoord;
						NumAllocatedPages++;
					}
				}
			}
		}

		// Initialize the SparseVolumeTexture page and tile.
		const int32 FormatSize[] = { GPixelFormats[Header.AttributesFormats[0]].BlockBytes, GPixelFormats[Header.AttributesFormats[1]].BlockBytes };
		PageTable.AddDefaulted();
		PageTable[MipLevel].SetNumZeroed(PageTableVolumeResolution.X * PageTableVolumeResolution.Y * PageTableVolumeResolution.Z);
		PhysicalTileDataA.AddZeroed(NumAllocatedPages * UE::SVT::SVTNumVoxelsPerTile * FormatSize[0]);
		PhysicalTileDataB.AddZeroed(NumAllocatedPages * UE::SVT::SVTNumVoxelsPerTile * FormatSize[1]);

		// Generate page table and tile volume data by splatting the data
		{
			uint32 DstTileIndex = (uint32)MipInfo.TileOffset;

			for (int32 i = 0; i < NumAllocatedPages; ++i)
			{
				const FIntVector3 PageCoordToSplat = LinearAllocatedPages[i];

				// Setup the page table entry
				PageTable[MipLevel]
					[
						PageCoordToSplat.Z * PageTableVolumeResolution.X * PageTableVolumeResolution.Y +
						PageCoordToSplat.Y * PageTableVolumeResolution.X +
						PageCoordToSplat.X
					] = DstTileIndex;

				// Write tile data
				const FIntVector3 ParentVolumeCoordBase = (PageTableVolumeAABBMin + PageCoordToSplat) * SPARSE_VOLUME_TILE_RES * 2;
				for (int32 AttributesIdx = 0; AttributesIdx < 2; ++AttributesIdx)
				{
					if ((AttributesIdx == 0 && PhysicalTileDataA.IsEmpty()) || (AttributesIdx == 1 && PhysicalTileDataB.IsEmpty()))
					{
						continue;
					}

					for (int32 Z = 0; Z < SPARSE_VOLUME_TILE_RES; ++Z)
					{
						for (int32 Y = 0; Y < SPARSE_VOLUME_TILE_RES; ++Y)
						{
							for (int32 X = 0; X < SPARSE_VOLUME_TILE_RES; ++X)
							{
								FVector4f DownsampledValue = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
								for (int32_t OffsetIdx = 0; OffsetIdx < 8; ++OffsetIdx)
								{
									const FIntVector3 Offset = FIntVector3(OffsetIdx, OffsetIdx >> 1, OffsetIdx >> 2) & 1;
									const FIntVector3 SourceCoord = ParentVolumeCoordBase + FIntVector3(X, Y, Z) * 2 + Offset;
									const FVector4f SampleValue = Load(SourceCoord, MipLevel - 1, AttributesIdx);
									DownsampledValue += SampleValue;
								}
								DownsampledValue /= 8.0f;

								const FIntVector3 WriteCoord = FIntVector3(X, Y, Z);
								WriteTileDataVoxel(DstTileIndex, WriteCoord, AttributesIdx, DownsampledValue);
							}
						}
					}
				}

				// Set the next tile to be written to
				++DstTileIndex;
			}
			MipInfo.TileCount = NumAllocatedPages;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////
