// Copyright Epic Games, Inc. All Rights Reserved.

#include "SparseVolumeTexture/SparseVolumeTextureData.h"
#include "SparseVolumeTexture/SparseVolumeTextureUtility.h"
#include "Async/ParallelFor.h"
#include "Hash/CityHash.h"

DEFINE_LOG_CATEGORY_STATIC(LogSparseVolumeTextureData, Log, All);

// Enabling this ensures proper bilinear filtering between physical pages and empty pages by tagging neighboring empty pages as resident/physical. This causes more physical tiles to be generated though.
#define SVT_CORRECT_TILE_ALLOCATION_FOR_LINEAR_FILTERING 0

////////////////////////////////////////////////////////////////////////////////////////////////

void FSparseVolumeTextureData::Serialize(FArchive& Ar)
{
	Header.Serialize(Ar);

	Ar << Version;

	if (Version == 0)
	{
		int32 NumMipLevels = MipMaps.Num();
		Ar << NumMipLevels;
		if (Ar.IsLoading())
		{
			MipMaps.Reset(NumMipLevels);
			MipMaps.SetNum(NumMipLevels);
		}
		for (int32 MipLevel = 0; MipLevel < NumMipLevels; ++MipLevel)
		{
			FMipMap& MipMap = MipMaps[MipLevel];
			Ar << MipMap.PageTable;
			Ar << MipMap.PhysicalTileDataA;
			Ar << MipMap.PhysicalTileDataB;
			Ar << MipMap.NumPhysicalTiles;
		}
	}
	else
	{
		// FSparseVolumeTextureData needs to account for new version
		check(false);
	}
}

bool FSparseVolumeTextureData::Construct(const ISparseVolumeTextureDataConstructionAdapter& Adapter)
{
	ISparseVolumeTextureDataConstructionAdapter::FAttributesInfo AttributesInfo[2];
	Adapter.GetAttributesInfo(AttributesInfo[0], AttributesInfo[1]);

	MipMaps.SetNum(1);
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

	if (PageTableVolumeResolution.X > UE::SVT::SVTMaxVolumeTextureDim
		|| PageTableVolumeResolution.Y > UE::SVT::SVTMaxVolumeTextureDim
		|| PageTableVolumeResolution.Z > UE::SVT::SVTMaxVolumeTextureDim)
	{
		UE_LOG(LogSparseVolumeTextureData, Warning, TEXT("SparseVolumeTexture page table texture dimensions exceed limit (%ix%ix%i): %ix%ix%i"),
			UE::SVT::SVTMaxVolumeTextureDim, UE::SVT::SVTMaxVolumeTextureDim, UE::SVT::SVTMaxVolumeTextureDim,
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
				check(Coord.X >= Header.VirtualVolumeAABBMin.X && Coord.Y >= Header.VirtualVolumeAABBMin.Y && Coord.Z >= Header.VirtualVolumeAABBMin.Z);
				check(Coord.X < Header.VirtualVolumeAABBMax.X && Coord.Y < Header.VirtualVolumeAABBMax.Y && Coord.Z < Header.VirtualVolumeAABBMax.Z);

#if SVT_CORRECT_TILE_ALLOCATION_FOR_LINEAR_FILTERING
				// Tag all pages touching the 3x3 area around this voxel in order to properly support bilinear/border voxels
				for (int32 OffsetZ = -1; OffsetZ < 2; ++OffsetZ)
				{
					for (int32 OffsetY = -1; OffsetY < 2; ++OffsetY)
					{
						for (int32 OffsetX = -1; OffsetX < 2; ++OffsetX)
						{
							const FIntVector3 GridCoord = Coord + FIntVector3(OffsetX, OffsetY, OffsetZ);
							if (GridCoord.X >= Header.VirtualVolumeAABBMin.X
								&& GridCoord.Y >= Header.VirtualVolumeAABBMin.Y 
								&& GridCoord.Z >= Header.VirtualVolumeAABBMin.Z
								&& GridCoord.X < Header.VirtualVolumeAABBMax.X
								&& GridCoord.Y < Header.VirtualVolumeAABBMax.Y
								&& GridCoord.Z < Header.VirtualVolumeAABBMax.Z)
							{
								const FIntVector3 PageCoord = (GridCoord / SPARSE_VOLUME_TILE_RES) - Header.PageTableVolumeAABBMin;
								check(PageCoord.X >= 0 && PageCoord.Y >= 0 && PageCoord.Z >= 0);
								check(PageCoord.X < Header.PageTableVolumeResolution.X && PageCoord.Y < Header.PageTableVolumeResolution.Y && PageCoord.Z < Header.PageTableVolumeResolution.Z);

								const int32 PageIndex = PageCoord.Z * (Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.X) + PageCoord.Y * Header.PageTableVolumeResolution.X + PageCoord.X;
								PagesWithData[PageIndex] = true;
							}
						}
					}
				}
#else // SVT_CORRECT_TILE_ALLOCATION_FOR_LINEAR_FILTERING
				const FIntVector3 GridCoord = Coord;
				const FIntVector3 PageCoord = (GridCoord / SPARSE_VOLUME_TILE_RES) - Header.PageTableVolumeAABBMin;
				check(PageCoord.X >= 0 && PageCoord.Y >= 0 && PageCoord.Z >= 0);
				check(PageCoord.X < Header.PageTableVolumeResolution.X && PageCoord.Y < Header.PageTableVolumeResolution.Y && PageCoord.Z < Header.PageTableVolumeResolution.Z);

				const int32 PageIndex = PageCoord.Z * (Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.X) + PageCoord.Y * Header.PageTableVolumeResolution.X + PageCoord.X;
				PagesWithData[PageIndex] = true;
#endif // SVT_CORRECT_TILE_ALLOCATION_FOR_LINEAR_FILTERING
			}
		});

	// Allocate some memory for temp data (worst case)
	TArray<FIntVector3> LinearAllocatedPages;
	LinearAllocatedPages.SetNum(Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.Z);

	// Go over each potential page from the source data and push allocate it if it has any data.
	// Otherwise point to the default empty page.
	int32 NumAllocatedPages = 0;
	for (int32 PageZ = 0; PageZ < Header.PageTableVolumeResolution.Z; ++PageZ)
	{
		for (int32 PageY = 0; PageY < Header.PageTableVolumeResolution.Y; ++PageY)
		{
			for (int32 PageX = 0; PageX < Header.PageTableVolumeResolution.X; ++PageX)
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
	LinearAllocatedPages.SetNum(NumAllocatedPages);

	// Initialize the SparseVolumeTexture page and tile.
	const int32 FormatSize[] = { GPixelFormats[Header.AttributesFormats[0]].BlockBytes, GPixelFormats[Header.AttributesFormats[1]].BlockBytes };
	MipMaps[0].PageTable.SetNumZeroed(Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.Z);
	MipMaps[0].PhysicalTileDataA.SetNum((int64)NumAllocatedPages * (int64)UE::SVT::SVTNumVoxelsPerPaddedTile * (int64)FormatSize[0]);
	MipMaps[0].PhysicalTileDataB.SetNum((int64)NumAllocatedPages * (int64)UE::SVT::SVTNumVoxelsPerPaddedTile * (int64)FormatSize[1]);
	MipMaps[0].NumPhysicalTiles = NumAllocatedPages;

	// Generate page table and tile volume data by splatting the data
	{
		// Fill all tiles with null tile data. See below for an explanation of why this is necessary.
		{
			// Compute potentially normalized fallback values
			FVector4f FallbackValues[] = { AttributesInfo[0].FallbackValue, AttributesInfo[1].FallbackValue };
			for (int32 AttributesIdx = 0; AttributesIdx < 2; ++AttributesIdx)
			{
				if (AttributesInfo[AttributesIdx].bNormalized)
				{
					FallbackValues[AttributesIdx] = FallbackValues[AttributesIdx] * AttributesInfo[AttributesIdx].NormalizeScale + AttributesInfo[AttributesIdx].NormalizeBias;
				}
			}
			Header.NullTileValues[0] = FallbackValues[0];
			Header.NullTileValues[1] = FallbackValues[1];

			// Fill all tiles with fallback values/null tile values. We can't guarantee that all voxels of a page will be written to in the next step, so if we don't do this,
			// we might end up with physical tiles that have only been partially filled with valid voxels.
			ParallelFor(NumAllocatedPages, [&FallbackValues, this](int32 TileIndex)
				{
					for (int32 Z = 0; Z < SPARSE_VOLUME_TILE_RES_PADDED; ++Z)
					{
						for (int32 Y = 0; Y < SPARSE_VOLUME_TILE_RES_PADDED; ++Y)
						{
							for (int32 X = 0; X < SPARSE_VOLUME_TILE_RES_PADDED; ++X)
							{
								const FIntVector3 WriteCoord = FIntVector3(X - 1, Y - 1, Z - 1);
								WriteTileDataVoxel(TileIndex, WriteCoord, 0 /*MipLevel*/, 0 /*AttributesIndex*/, FallbackValues[0]);
								WriteTileDataVoxel(TileIndex, WriteCoord, 0 /*MipLevel*/, 1 /*AttributesIndex*/, FallbackValues[1]);
							}
						}
					}
				});

			Header.NullTileValuesQuantized[0] = ReadTileDataVoxel(0, FIntVector::ZeroValue, 0 /*MipLevel*/, 0 /*AttributesIndex*/);
			Header.NullTileValuesQuantized[1] = ReadTileDataVoxel(0, FIntVector::ZeroValue, 0 /*MipLevel*/, 1 /*AttributesIndex*/);
		}

		// Set up the page table entries
		for (int32 i = 0; i < NumAllocatedPages; ++i)
		{
			const FIntVector3 PageCoordToSplat = LinearAllocatedPages[i];
			const int32 TileIndex = (PageCoordToSplat.Z * Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y) + (PageCoordToSplat.Y * Header.PageTableVolumeResolution.X) + PageCoordToSplat.X;
			MipMaps[0].PageTable[TileIndex] = (uint32)(i + 1); // +1 because 0 is reserved for the null tile which we don't actually store in memory
		}
	}

	// Write physical tile data
	Adapter.IteratePhysicalSource([&](const FIntVector3& Coord, int32 AttributesIdx, int32 ComponentIdx, float VoxelValue)
		{
			const FIntVector3 GridCoord = Coord;
			check(GridCoord.X >= 0 && GridCoord.Y >= 0 && GridCoord.Z >= 0);
			check(GridCoord.X < Header.VirtualVolumeAABBMax.X && GridCoord.Y < Header.VirtualVolumeAABBMax.Y && GridCoord.Z < Header.VirtualVolumeAABBMax.Z);
			const FIntVector3 PageCoord = (GridCoord / SPARSE_VOLUME_TILE_RES) - Header.PageTableVolumeAABBMin;
			check(PageCoord.X >= 0 && PageCoord.Y >= 0 && PageCoord.Z >= 0);
			check(PageCoord.X < Header.PageTableVolumeResolution.X && PageCoord.Y < Header.PageTableVolumeResolution.Y && PageCoord.Z < Header.PageTableVolumeResolution.Z);

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

			const int32 TileIndex = (int32)MipMaps[0].PageTable[PageIndex];
			const FIntVector3 TileLocalCoord = GridCoord % SPARSE_VOLUME_TILE_RES;
			WriteTileDataVoxel(TileIndex - 1, TileLocalCoord, 0 /*MipLevel*/, AttributesIdx, FVector4f(WriteValue, WriteValue, WriteValue, WriteValue), ComponentIdx);  // -1 because 0 is reserved for the null tile which we don't actually store in memory
		});

	return true;
}

uint32 FSparseVolumeTextureData::ReadPageTable(const FIntVector3& PageTableCoord, int32 MipLevel) const
{
	if (!MipMaps.IsValidIndex(MipLevel))
	{
		return INDEX_NONE;
	}
	const FIntVector3 PageTableResolution = FIntVector3(
		FMath::Max(1, Header.PageTableVolumeResolution.X >> MipLevel),
		FMath::Max(1, Header.PageTableVolumeResolution.Y >> MipLevel),
		FMath::Max(1, Header.PageTableVolumeResolution.Z >> MipLevel));
	if (PageTableCoord.X < 0 || PageTableCoord.Y < 0 || PageTableCoord.Z < 0
		|| PageTableCoord.X >= PageTableResolution.X
		|| PageTableCoord.Y >= PageTableResolution.Y
		|| PageTableCoord.Z >= PageTableResolution.Z)
	{
		return INDEX_NONE;
	}

	const int32 PageIndex = PageTableCoord.Z * (PageTableResolution.Y * PageTableResolution.X) + PageTableCoord.Y * PageTableResolution.X + PageTableCoord.X;
	if (MipMaps[MipLevel].PageTable.IsValidIndex(PageIndex))
	{
		return MipMaps[MipLevel].PageTable[PageIndex];
	}
	return INDEX_NONE;
}

FVector4f FSparseVolumeTextureData::ReadTileDataVoxel(int32 TileIndex, const FIntVector3& TileDataCoord, int32 MipLevel, int32 AttributesIdx) const
{
	check(AttributesIdx >= 0 && AttributesIdx <= 1);
	if (TileIndex == INDEX_NONE || (AttributesIdx == 0 && MipMaps[MipLevel].PhysicalTileDataA.IsEmpty()) || (AttributesIdx == 1 && MipMaps[MipLevel].PhysicalTileDataB.IsEmpty()))
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

	const FInt64Vector3 CoordPadded = FInt64Vector3(TileDataCoord) + FInt64Vector3(SPARSE_VOLUME_TILE_BORDER);
	const int64 VoxelIndex = (int64)TileIndex * (int64)UE::SVT::SVTNumVoxelsPerPaddedTile + CoordPadded.Z * (SPARSE_VOLUME_TILE_RES_PADDED * SPARSE_VOLUME_TILE_RES_PADDED) + CoordPadded.Y * SPARSE_VOLUME_TILE_RES_PADDED + CoordPadded.X;
	const uint8* TileData = AttributesIdx == 0 ? MipMaps[MipLevel].PhysicalTileDataA.GetData() : MipMaps[MipLevel].PhysicalTileDataB.GetData();
	const EPixelFormat Format = Header.AttributesFormats[AttributesIdx];
	return UE::SVT::ReadVoxel(VoxelIndex, TileData, Format);
}

FVector4f FSparseVolumeTextureData::Load(const FIntVector3& VolumeCoord, int32 MipLevel, int32 AttributesIdx, const FSparseVolumeTextureDataAddressingInfo& AddressingInfo) const
{
	auto ApplyAddressMode = [](int32 x, int32 Width, TextureAddress Mode)
	{
		switch (Mode)
		{
		case TA_Wrap:
			return (x % Width + Width) % Width; // Make sure it's a proper modulo for negative numbers ....
		case TA_Clamp:
			return FMath::Clamp(x, 0, Width - 1);
		case TA_Mirror:
			int32 DoubleWidth = Width + Width;
			int32 DoubleWrap = (x % DoubleWidth + DoubleWidth) % DoubleWidth;
			return (DoubleWrap < Width) ? DoubleWrap : (Width - 1) - (DoubleWrap - Width);
		}
		return x;
	};
	if (!MipMaps.IsValidIndex(MipLevel))
	{
		return FVector4f();
	}
	const FIntVector3 AddressModeVolumeCoord = FIntVector3(
		ApplyAddressMode(VolumeCoord.X, FMath::Max(1, AddressingInfo.VolumeResolution.X >> MipLevel), AddressingInfo.AddressX),
		ApplyAddressMode(VolumeCoord.Y, FMath::Max(1, AddressingInfo.VolumeResolution.Y >> MipLevel), AddressingInfo.AddressY), 
		ApplyAddressMode(VolumeCoord.Z, FMath::Max(1, AddressingInfo.VolumeResolution.Z >> MipLevel), AddressingInfo.AddressZ));
	const FIntVector3 PageTableCoord = (AddressModeVolumeCoord / SPARSE_VOLUME_TILE_RES) - (Header.PageTableVolumeAABBMin >> MipLevel);
	const uint32 TileIndex = ReadPageTable(PageTableCoord, MipLevel);
	if (TileIndex == 0)
	{
		return Header.NullTileValuesQuantized[AttributesIdx];
	}
	const FIntVector3 VoxelCoord = AddressModeVolumeCoord % SPARSE_VOLUME_TILE_RES;
	const FVector4f Sample = ReadTileDataVoxel(TileIndex - 1, VoxelCoord, MipLevel, AttributesIdx); // -1 because we don't store the null tile in memory.
	return Sample;
}

void FSparseVolumeTextureData::WriteTileDataVoxel(int32 TileIndex, const FIntVector3& TileDataCoord, int32 MipLevel, int32 AttributesIdx, const FVector4f& Value, int32 DstComponent)
{
	check(AttributesIdx >= 0 && AttributesIdx <= 1);
	if ((AttributesIdx == 0 && MipMaps[MipLevel].PhysicalTileDataA.IsEmpty()) || (AttributesIdx == 1 && MipMaps[MipLevel].PhysicalTileDataB.IsEmpty()))
	{
		return;
	}

	// SV_TODO check for TileIndex oob

	// Allow TileDataCoord to extend past SPARSE_VOLUME_TILE_RES by SPARSE_VOLUME_TILE_BORDER.
	// This allows us to write the border voxels without changing how TileDataCoord needs to be computed for other use cases.
	if (TileDataCoord.X < -SPARSE_VOLUME_TILE_BORDER
		|| TileDataCoord.Y < -SPARSE_VOLUME_TILE_BORDER
		|| TileDataCoord.Z < -SPARSE_VOLUME_TILE_BORDER
		|| TileDataCoord.X >= (SPARSE_VOLUME_TILE_RES + SPARSE_VOLUME_TILE_BORDER)
		|| TileDataCoord.Y >= (SPARSE_VOLUME_TILE_RES + SPARSE_VOLUME_TILE_BORDER)
		|| TileDataCoord.Z >= (SPARSE_VOLUME_TILE_RES + SPARSE_VOLUME_TILE_BORDER))
	{
		return;
	}

	const FInt64Vector3 CoordPadded = FInt64Vector3(TileDataCoord) + FInt64Vector3(SPARSE_VOLUME_TILE_BORDER);
	const int64 VoxelIndex = (int64)TileIndex * (int64)UE::SVT::SVTNumVoxelsPerPaddedTile + CoordPadded.Z * (SPARSE_VOLUME_TILE_RES_PADDED * SPARSE_VOLUME_TILE_RES_PADDED) + CoordPadded.Y * SPARSE_VOLUME_TILE_RES_PADDED + CoordPadded.X;
	uint8* TileData = AttributesIdx == 0 ? MipMaps[MipLevel].PhysicalTileDataA.GetData() : MipMaps[MipLevel].PhysicalTileDataB.GetData();
	const EPixelFormat Format = Header.AttributesFormats[AttributesIdx];
	UE::SVT::WriteVoxel(VoxelIndex, TileData, Format, Value, DstComponent);
}

bool FSparseVolumeTextureData::GenerateMipMaps(const FSparseVolumeTextureDataAddressingInfo& AddressingInfo, int32 NumMipLevels)
{
	const int32 FormatSize[] = { GPixelFormats[Header.AttributesFormats[0]].BlockBytes, GPixelFormats[Header.AttributesFormats[1]].BlockBytes };

	check(!MipMaps.IsEmpty());
	check(FMath::IsPowerOfTwo(Header.PageTableVolumeResolution.X)
		&& FMath::IsPowerOfTwo(Header.PageTableVolumeResolution.Y)
		&& FMath::IsPowerOfTwo(Header.PageTableVolumeResolution.Z));
	const int32 NumMip0Tiles = MipMaps[0].NumPhysicalTiles;
	check(MipMaps[0].PhysicalTileDataA.Num() >= ((int64)NumMip0Tiles * (int64)UE::SVT::SVTNumVoxelsPerPaddedTile * (int64)FormatSize[0]));
	check(MipMaps[0].PhysicalTileDataB.Num() >= ((int64)NumMip0Tiles * (int64)UE::SVT::SVTNumVoxelsPerPaddedTile * (int64)FormatSize[1]));
	check(MipMaps[0].PageTable.Num() == (Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.Z));
	
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

	MipMaps.SetNum(1);
	MipMaps[0].PhysicalTileDataA.SetNum((int64)NumMip0Tiles * (int64)UE::SVT::SVTNumVoxelsPerPaddedTile * (int64)FormatSize[0]);
	MipMaps[0].PhysicalTileDataB.SetNum((int64)NumMip0Tiles * (int64)UE::SVT::SVTNumVoxelsPerPaddedTile * (int64)FormatSize[1]);

	for (int32 MipLevel = 1; MipLevel < NumMipLevels; ++MipLevel)
	{
		MipMaps.AddDefaulted();

		const FIntVector3 PageTableVolumeAABBMin = (Header.VirtualVolumeAABBMin / SPARSE_VOLUME_TILE_RES) >> MipLevel;
		const FIntVector3 ParentPageTableVolumeAABBMin = (Header.VirtualVolumeAABBMin / SPARSE_VOLUME_TILE_RES) >> (MipLevel - 1);
		const FIntVector3 PageTableVolumeResolution = FIntVector3(
			FMath::Max(1, Header.PageTableVolumeResolution.X >> MipLevel), 
			FMath::Max(1, Header.PageTableVolumeResolution.Y >> MipLevel), 
			FMath::Max(1, Header.PageTableVolumeResolution.Z >> MipLevel));
		
		check(FMath::IsPowerOfTwo(PageTableVolumeResolution.X)
			&& FMath::IsPowerOfTwo(PageTableVolumeResolution.Y)
			&& FMath::IsPowerOfTwo(PageTableVolumeResolution.Z));

		// Allocate some memory for temp data (worst case)
		TArray<FIntVector3> LinearAllocatedPages;
		LinearAllocatedPages.SetNum(PageTableVolumeResolution.X * PageTableVolumeResolution.Y * PageTableVolumeResolution.Z);

		// Go over each potential page from the source data and push allocate it if it has any data.
		// Otherwise point to the default empty page.
		int32 NumAllocatedPages = 0;
		for (int32 PageZ = 0; PageZ < PageTableVolumeResolution.Z; ++PageZ)
		{
			for (int32 PageY = 0; PageY < PageTableVolumeResolution.Y; ++PageY)
			{
				for (int32 PageX = 0; PageX < PageTableVolumeResolution.X; ++PageX)
				{
					const FIntVector3 PageCoord = FIntVector3(PageX, PageY, PageZ);
					bool bHasAnyData = false;
					for (int32 OffsetIdx = 0; OffsetIdx < 8; ++OffsetIdx)
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
		LinearAllocatedPages.SetNum(NumAllocatedPages);

		// Initialize the SparseVolumeTexture page and tile.
		MipMaps[MipLevel].PageTable.SetNumZeroed(PageTableVolumeResolution.X * PageTableVolumeResolution.Y * PageTableVolumeResolution.Z);
		MipMaps[MipLevel].PhysicalTileDataA.SetNum((int64)NumAllocatedPages * (int64)UE::SVT::SVTNumVoxelsPerPaddedTile * (int64)FormatSize[0]);
		MipMaps[MipLevel].PhysicalTileDataB.SetNum((int64)NumAllocatedPages * (int64)UE::SVT::SVTNumVoxelsPerPaddedTile * (int64)FormatSize[1]);
		MipMaps[MipLevel].NumPhysicalTiles = NumAllocatedPages;

		// Generate page table and tile volume data by splatting the data
		{
			ParallelFor(NumAllocatedPages, [this, &LinearAllocatedPages, &PageTableVolumeResolution, &PageTableVolumeAABBMin, &AddressingInfo, MipLevel](int32 PageIndex)
				{
					const FIntVector3 PageCoordToSplat = LinearAllocatedPages[PageIndex];
					const uint32 DstTileIndex = PageIndex + 1; // Page table entries are relative to the mip level, but 0 is reserved for null tile

					// Setup the page table entry
					MipMaps[MipLevel].PageTable
						[
							PageCoordToSplat.Z * PageTableVolumeResolution.X * PageTableVolumeResolution.Y +
							PageCoordToSplat.Y * PageTableVolumeResolution.X +
							PageCoordToSplat.X
						] = DstTileIndex;

					// Write tile data
					const FIntVector3 ParentVolumeCoordBase = (PageTableVolumeAABBMin + PageCoordToSplat) * SPARSE_VOLUME_TILE_RES * 2;
					for (int32 AttributesIdx = 0; AttributesIdx < 2; ++AttributesIdx)
					{
						if (Header.AttributesFormats[AttributesIdx] == PF_Unknown)
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
									for (int32 OffsetIdx = 0; OffsetIdx < 8; ++OffsetIdx)
									{
										const FIntVector3 Offset = FIntVector3(OffsetIdx, OffsetIdx >> 1, OffsetIdx >> 2) & 1;
										const FIntVector3 SourceCoord = ParentVolumeCoordBase + FIntVector3(X, Y, Z) * 2 + Offset;
										const FVector4f SampleValue = Load(SourceCoord, MipLevel - 1, AttributesIdx, AddressingInfo);
										DownsampledValue += SampleValue;
									}
									DownsampledValue /= 8.0f;

									const FIntVector3 WriteCoord = FIntVector3(X, Y, Z);
									WriteTileDataVoxel(DstTileIndex - 1, WriteCoord, MipLevel, AttributesIdx, DownsampledValue); // -1 because we don't store the null tile in memory.
								}
							}
						}
					}
				});
		}

		if (!GenerateBorderVoxels(AddressingInfo, MipLevel, LinearAllocatedPages))
		{
			return false;
		}
	}

	return true;
}

bool FSparseVolumeTextureData::GenerateBorderVoxels(const FSparseVolumeTextureDataAddressingInfo& AddressingInfo, int32 MipLevel, const TArray<FIntVector3>& PageCoords)
{
	const FIntVector3 PageTableVolumeResolution = FIntVector3(
		FMath::Max(1, Header.PageTableVolumeResolution.X >> MipLevel), 
		FMath::Max(1, Header.PageTableVolumeResolution.Y >> MipLevel), 
		FMath::Max(1, Header.PageTableVolumeResolution.Z >> MipLevel));

	ParallelFor(PageCoords.Num(), [this, MipLevel, &PageCoords, &PageTableVolumeResolution, &AddressingInfo](int32 PageIndex)
		{
			const FIntVector3& PageCoord = PageCoords[PageIndex];
			const int32 PageTableIndex = (PageCoord.Z * PageTableVolumeResolution.X * PageTableVolumeResolution.Y) + (PageCoord.Y * PageTableVolumeResolution.X) + PageCoord.X;
			const int32 DstTileIndex = (int32)MipMaps[MipLevel].PageTable[PageTableIndex] - 1; // -1 because we don't actually store the null tile in memory
			const FIntVector3 PageTableOffset = (Header.PageTableVolumeAABBMin >> MipLevel);
			for (int32 Z = -SPARSE_VOLUME_TILE_BORDER; Z < (SPARSE_VOLUME_TILE_RES + SPARSE_VOLUME_TILE_BORDER); ++Z)
			{
				for (int32 Y = -SPARSE_VOLUME_TILE_BORDER; Y < (SPARSE_VOLUME_TILE_RES + SPARSE_VOLUME_TILE_BORDER); ++Y)
				{
					for (int32 X = -SPARSE_VOLUME_TILE_BORDER; X < (SPARSE_VOLUME_TILE_RES + SPARSE_VOLUME_TILE_BORDER); ++X)
					{
						if (X < 0 || Y < 0 || Z < 0 || X >= SPARSE_VOLUME_TILE_RES || Y >= SPARSE_VOLUME_TILE_RES || Z >= SPARSE_VOLUME_TILE_RES)
						{
							const FIntVector3 LocalCoord = FIntVector3(X, Y, Z);
							const FIntVector3 VolumeCoord = (PageTableOffset + PageCoord) * SPARSE_VOLUME_TILE_RES + LocalCoord;
							const FVector4f Border0 = Load(VolumeCoord, MipLevel, 0, AddressingInfo);
							const FVector4f Border1 = Load(VolumeCoord, MipLevel, 1, AddressingInfo);
							WriteTileDataVoxel(DstTileIndex, LocalCoord, MipLevel, 0, Border0);
							WriteTileDataVoxel(DstTileIndex, LocalCoord, MipLevel, 1, Border1);
						}
					}
				}
			}
		});

	return true;
}

bool FSparseVolumeTextureData::DeduplicateTiles()
{
	const int32 NumMipLevels = MipMaps.Num();
	const int32 FormatSize[] = { GPixelFormats[Header.AttributesFormats[0]].BlockBytes, GPixelFormats[Header.AttributesFormats[1]].BlockBytes };
	const int32 TileByteSize[] = { UE::SVT::SVTNumVoxelsPerPaddedTile * FormatSize[0], UE::SVT::SVTNumVoxelsPerPaddedTile * FormatSize[1] };

	int32 NumProcessedTiles = 0;
	int32 NumSurvivingTiles = 0;

	TArray<uint64> TileHashes; // Reused between mip level iterations
	TArray<uint32> TileRemap; // Reused between mip level iterations

	// Deduplicate each mip level individually
	for (int32 MipLevel = 0; MipLevel < NumMipLevels; ++MipLevel)
	{
		TileHashes.Reset(MipMaps[MipLevel].NumPhysicalTiles);
		TileRemap.Reset(MipMaps[MipLevel].NumPhysicalTiles);
		uint8* PhysicalTileData[] = { MipMaps[MipLevel].PhysicalTileDataA.GetData(), MipMaps[MipLevel].PhysicalTileDataB.GetData() };
		int32 TileWritePos = 0;

		// Compute a hash per tile and compare against previous hashes.
		for (int32 TileIdx = 0; TileIdx < MipMaps[MipLevel].NumPhysicalTiles; ++TileIdx)
		{
			uint64 Hash = 0;
			if (FormatSize[0])
			{
				Hash = CityHash64((const char*)PhysicalTileData[0] + (SIZE_T)TileIdx * (SIZE_T)TileByteSize[0], (uint32)TileByteSize[0]);
			}
			if (FormatSize[1])
			{
				Hash = CityHash64WithSeed((const char*)PhysicalTileData[1] + (SIZE_T)TileIdx * (SIZE_T)TileByteSize[1], (uint32)TileByteSize[1], Hash);
			}

			const int32 OldNumUniqueTiles = TileHashes.Num();
			const uint32 RemapIndex = TileHashes.AddUnique(Hash) + 1; // +1 because of implicit null tile at 0
			TileRemap.Add(RemapIndex);
			
			const bool bAddedNewUniqueTile = TileHashes.Num() > OldNumUniqueTiles;
			
			// Move tile to compacted position
			if (bAddedNewUniqueTile)
			{
				if (TileWritePos < TileIdx)
				{
					if (FormatSize[0])
					{
						FMemory::Memcpy(PhysicalTileData[0] + (SIZE_T)TileWritePos * (SIZE_T)TileByteSize[0], PhysicalTileData[0] + (SIZE_T)TileIdx * (SIZE_T)TileByteSize[0], (SIZE_T)TileByteSize[0]);
					}
					if (FormatSize[1])
					{
						FMemory::Memcpy(PhysicalTileData[1] + (SIZE_T)TileWritePos * (SIZE_T)TileByteSize[1], PhysicalTileData[1] + (SIZE_T)TileIdx * (SIZE_T)TileByteSize[1], (SIZE_T)TileByteSize[1]);
					}
				}
				else
				{
					check(TileWritePos == TileIdx);
				}
				++TileWritePos;
				++NumSurvivingTiles;
			}

			++NumProcessedTiles;
		}

		// Update mip info
		MipMaps[MipLevel].NumPhysicalTiles = TileWritePos;

		// Fix up page table
		for (uint32& PageTableEntry : MipMaps[MipLevel].PageTable)
		{
			if (PageTableEntry != 0)
			{
				PageTableEntry = TileRemap[PageTableEntry - 1]; // -1 because of implicit null tile at 0
			}
		}

		const uint64 DeduplicatedTileSizeBytes[] = { (uint64)TileWritePos * (uint64)TileByteSize[0], (uint64)TileWritePos * (uint64)TileByteSize[1] };
		if (DeduplicatedTileSizeBytes[0] > INT32_MAX || DeduplicatedTileSizeBytes[1] > INT32_MAX)
		{
			UE_LOG(LogSparseVolumeTextureData, Error, TEXT("SparseVolumeTexture still requires too much memory after tile deduplication! Physical tile data A: %llu bytes. Physical tile data B: %llu bytes"),
				(unsigned long long)DeduplicatedTileSizeBytes[0], (unsigned long long)DeduplicatedTileSizeBytes[1]);
			return false;
		}

		if (!MipMaps[MipLevel].PhysicalTileDataA.IsEmpty())
		{
			MipMaps[MipLevel].PhysicalTileDataA.SetNum(TileWritePos * TileByteSize[0]);
		}
		if (!MipMaps[MipLevel].PhysicalTileDataB.IsEmpty())
		{
			MipMaps[MipLevel].PhysicalTileDataB.SetNum(TileWritePos * TileByteSize[1]);
		}
	}

	const int32 NumRemovedTiles = NumProcessedTiles - NumSurvivingTiles;
	const float RemovedTilesPercentage = ((float)NumRemovedTiles / (float)NumProcessedTiles) * 100.0f;
	UE_LOG(LogSparseVolumeTextureData, Display, TEXT("SparseVolumeTexture tile deduplication removed %i tiles out of %i (%f%%)"), NumRemovedTiles, NumProcessedTiles, RemovedTilesPercentage);

	return true;
}

bool FSparseVolumeTextureData::BuildDerivedData(const FSparseVolumeTextureDataAddressingInfo& AddressingInfo, int32 NumMipLevels, bool bMoveMip0FromThis, FSparseVolumeTextureData& OutDerivedData)
{
	OutDerivedData = FSparseVolumeTextureData{};
	OutDerivedData.Header = Header;
	OutDerivedData.MipMaps.SetNum(1);
	if (bMoveMip0FromThis)
	{
		OutDerivedData.MipMaps[0] = MoveTemp(MipMaps[0]);
		MipMaps[0] = {};
	}
	else
	{
		OutDerivedData.MipMaps[0] = MipMaps[0];
	}

	// Generate border voxels of mip0
	{
		// Collect the page table coordinates of all non-zero pages
		TArray<FIntVector3> PageCoords;
		for (int32 PageZ = 0; PageZ < Header.PageTableVolumeResolution.Z; ++PageZ)
		{
			for (int32 PageY = 0; PageY < Header.PageTableVolumeResolution.Y; ++PageY)
			{
				for (int32 PageX = 0; PageX < Header.PageTableVolumeResolution.X; ++PageX)
				{
					const int32 PageIndex = PageZ * (Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.X) + PageY * Header.PageTableVolumeResolution.X + PageX;
					const uint32 PageTableEntry = OutDerivedData.MipMaps[0].PageTable[PageIndex];
					if (PageTableEntry != 0)
					{
						PageCoords.Add(FIntVector3(PageX, PageY, PageZ));
					}
				}
			}
		}

		if (!OutDerivedData.GenerateBorderVoxels(AddressingInfo, 0, PageCoords))
		{
			return false;
		}
	}

	// Generate all remaining mips. Also generates border voxels.
	if (!OutDerivedData.GenerateMipMaps(AddressingInfo, NumMipLevels))
	{
		return false;
	}

	// Deduplicate tiles by potentially having multiple pages point to the same physical tile
	if (!OutDerivedData.DeduplicateTiles())
	{
		return false;
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////
