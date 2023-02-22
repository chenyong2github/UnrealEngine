// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SparseVolumeTexture.cpp: SparseVolumeTexture implementation.
=============================================================================*/

#include "SparseVolumeTexture/SparseVolumeTexture.h"

#include "Misc/SecureHash.h"
#include "EngineUtils.h"
#include "Shader/ShaderTypes.h"
#include "RenderingThread.h"

#if WITH_EDITORONLY_DATA
#include "DerivedDataCache.h"
#include "DerivedDataRequestOwner.h"
#endif

#include "Serialization/LargeMemoryReader.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/BulkDataReader.h"
#include "Serialization/BulkDataWriter.h"
#include "Serialization/EditorBulkDataReader.h"

#include "ContentStreaming.h"

#define LOCTEXT_NAMESPACE "USparseVolumeTexture"

DEFINE_LOG_CATEGORY_STATIC(LogSparseVolumeTexture, Log, All);

// We are using this instead of GMaxVolumeTextureDimensions to be independent of the platform that
// the asset is imported on. 2048 should be a safe value that should be supported by all our platforms.
static constexpr int32 SVTMaxVolumeTextureDim = 2048;

namespace UE
{
namespace SVT
{
namespace Private
{
	// SVT_TODO: This really should be a shared function.
	template <typename Y, typename T>
	void SerializeEnumAs(FArchive& Ar, T& Target)
	{
		Y Buffer = static_cast<Y>(Target);
		Ar << Buffer;
		if (Ar.IsLoading())
		{
			Target = static_cast<T>(Buffer);
		}
	}

	static float F16ToF32(uint16 Packed)
	{
		FFloat16 F16{};
		F16.Encoded = Packed;
		return F16.GetFloat();
	};

	static FIntVector3 AdvanceTileCoord(const FIntVector3& TileCoord, const FIntVector3& TileCoordResolution)
	{
		FIntVector3 Result = TileCoord;
		Result.X++;
		if (Result.X >= TileCoordResolution.X)
		{
			Result.X = 0;
			Result.Y++;
		}
		if (Result.Y >= TileCoordResolution.Y)
		{
			Result.Y = 0;
			Result.Z++;
		}
		return Result;
	};

	static FIntVector3 ComputeTileDataVolumeResolution(int32 NumAllocatedPages, bool bAnyEmptyPageExists)
	{
		const int32 EffectivelyAllocatedPageEntries = NumAllocatedPages + (bAnyEmptyPageExists ? 1 : 0);
		int32 TileVolumeResolutionCube = 1;
		while (TileVolumeResolutionCube * TileVolumeResolutionCube * TileVolumeResolutionCube < EffectivelyAllocatedPageEntries)
		{
			TileVolumeResolutionCube++;				// We use a simple loop to compute the minimum resolution of a cube to store all the tile data
		}
		FIntVector3 TileDataVolumeResolution = FIntVector3(TileVolumeResolutionCube, TileVolumeResolutionCube, TileVolumeResolutionCube);
		while (TileDataVolumeResolution.X * TileDataVolumeResolution.Y * (TileDataVolumeResolution.Z - 1) > EffectivelyAllocatedPageEntries)
		{
			TileDataVolumeResolution.Z--;	// We then trim an edge to get back space.
		}

		return TileDataVolumeResolution * SPARSE_VOLUME_TILE_RES;
	}
} // Private
} // SVT
} // UE

uint32 SparseVolumeTexturePackPageTableEntry(const FIntVector3& Coord)
{
	// A page encodes the physical tile coord as unsigned int of 11 11 10 bits
	// This means a page coord cannot be larger than 2047 for x and y and 1023 for z
	// which mean we cannot have more than 2048*2048*1024 = 4 Giga tiles of 16^3 tiles.
	uint32 Result = (Coord.X & 0x7FF) | ((Coord.Y & 0x7FF) << 11) | ((Coord.Z & 0x3FF) << 22);
	return Result;
}

FIntVector3 SparseVolumeTextureUnpackPageTableEntry(uint32 Packed)
{
	FIntVector3 Result;
	Result.X = Packed & 0x7FF;
	Result.Y = (Packed >> 11) & 0x7FF;
	Result.Z = (Packed >> 22) & 0x3FF;
	return Result;
}

////////////////////////////////////////////////////////////////////////////////////////////////

void FSparseVolumeRawSource::Serialize(FArchive& Ar)
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
		// FSparseVolumeRawSource needs to account for new version
		check(false);
	}
}

bool FSparseVolumeRawSource::Construct(const ISparseVolumeRawSourceConstructionAdapter& Adapter)
{
	ISparseVolumeRawSourceConstructionAdapter::FAttributesInfo AttributesInfo[2];
	Adapter.GetAttributesInfo(AttributesInfo[0], AttributesInfo[1]);

	Header.VirtualVolumeResolution = Adapter.GetResolution();
	Header.VirtualVolumeAABBMin = Adapter.GetAABBMin();
	Header.VirtualVolumeAABBMax = Adapter.GetAABBMax();
	Header.AttributesFormats[0] = AttributesInfo[0].Format;
	Header.AttributesFormats[1] = AttributesInfo[1].Format;

	Header.PageTableVolumeAABBMin = Header.VirtualVolumeAABBMin / SPARSE_VOLUME_TILE_RES;
	Header.PageTableVolumeAABBMax = (Header.VirtualVolumeAABBMax + FIntVector3(SPARSE_VOLUME_TILE_RES - 1)) / SPARSE_VOLUME_TILE_RES;
	Header.bHasNullTile = false;

	const FIntVector3 PageTableVolumeResolution = Header.PageTableVolumeAABBMax - Header.PageTableVolumeAABBMin;

	if (PageTableVolumeResolution.X > SVTMaxVolumeTextureDim
		|| PageTableVolumeResolution.Y > SVTMaxVolumeTextureDim
		|| PageTableVolumeResolution.Z > SVTMaxVolumeTextureDim)
	{
		UE_LOG(LogSparseVolumeTexture, Warning, TEXT("SparseVolumeTexture page table texture dimensions exceed limit (%ix%ix%i): %ix%ix%i"), 
			SVTMaxVolumeTextureDim, SVTMaxVolumeTextureDim, SVTMaxVolumeTextureDim, 
			PageTableVolumeResolution.X, PageTableVolumeResolution.Y, PageTableVolumeResolution.Z);
		return false;
	}

	Header.PageTableVolumeResolution = PageTableVolumeResolution;
	Header.TileDataVolumeResolution = FIntVector::ZeroValue;	// unknown for now

	// Tag all pages with valid data
	TBitArray PagesWithData(false, Header.PageTableVolumeResolution.Z * Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.X);
	Adapter.IteratePhysicalSource([&](const FIntVector3& Coord, int32 AttributesIdx, int32 ComponentIdx, float VoxelValue)
		{
			const bool bIsFallbackValue = (VoxelValue == AttributesInfo[AttributesIdx].FallbackValue[ComponentIdx]);
			if (!bIsFallbackValue)
			{
				const FIntVector3 GridCoord = Coord;
				check(GridCoord.X >= 0 && GridCoord.Y >= 0 && GridCoord.Z >= 0);
				check(GridCoord.X < Header.VirtualVolumeAABBMax.X && GridCoord.Y < Header.VirtualVolumeAABBMax.Y && GridCoord.Z < Header.VirtualVolumeAABBMax.Z);
				const FIntVector3 PageCoord = (GridCoord / SPARSE_VOLUME_TILE_RES) - Header.PageTableVolumeAABBMin;
				check(PageCoord.X >= 0 && PageCoord.Y >= 0 && PageCoord.Z >= 0);
				check(PageCoord.X < Header.PageTableVolumeResolution.X && PageCoord.Y < Header.PageTableVolumeResolution.Y && PageCoord.Z < Header.PageTableVolumeResolution.Z);

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
				Header.bHasNullTile |= !bHasAnyData;
			}
		}
	}

	// Compute Page and Tile VolumeResolution from allocated pages
	Header.TileDataVolumeResolution = UE::SVT::Private::ComputeTileDataVolumeResolution(NumAllocatedPages, Header.bHasNullTile);
	const FIntVector3 TileCoordResolution = Header.TileDataVolumeResolution / SPARSE_VOLUME_TILE_RES;

	if (Header.TileDataVolumeResolution.X > SVTMaxVolumeTextureDim
		|| Header.TileDataVolumeResolution.Y > SVTMaxVolumeTextureDim
		|| Header.TileDataVolumeResolution.Z > SVTMaxVolumeTextureDim)
	{
		UE_LOG(LogSparseVolumeTexture, Warning, TEXT("SparseVolumeTexture tile data texture dimensions exceed limit (%ix%ix%i): %ix%ix%i"), SVTMaxVolumeTextureDim, SVTMaxVolumeTextureDim, SVTMaxVolumeTextureDim, Header.TileDataVolumeResolution.X, Header.TileDataVolumeResolution.Y, Header.TileDataVolumeResolution.Z);
		return false;
	}

	// Initialize the SparseVolumeTexture page and tile.
	const int32 FormatSize[] = { GPixelFormats[(SIZE_T)Header.AttributesFormats[0]].BlockBytes, GPixelFormats[(SIZE_T)Header.AttributesFormats[1]].BlockBytes };
	PageTable.SetNumZeroed(Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.Z);
	PhysicalTileDataA.SetNumZeroed(Header.TileDataVolumeResolution.X * Header.TileDataVolumeResolution.Y * Header.TileDataVolumeResolution.Z * FormatSize[0]);
	PhysicalTileDataB.SetNumZeroed(Header.TileDataVolumeResolution.X * Header.TileDataVolumeResolution.Y * Header.TileDataVolumeResolution.Z * FormatSize[1]);

	// Generate page table and tile volume data by splatting the data
	{
		FIntVector3 DstTileCoord = FIntVector3::ZeroValue;

		// Add an empty tile is needed, reserve slot at coord 0
		if (Header.bHasNullTile)
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
			FillNullTile(FallbackValues[0], FallbackValues[1]);

			// PageTable is all cleared to zero, simply skip a tile
			DstTileCoord = UE::SVT::Private::AdvanceTileCoord(DstTileCoord, TileCoordResolution);
		}

		for (int32 i = 0; i < NumAllocatedPages; ++i)
		{
			const FIntVector3 PageCoordToSplat = LinearAllocatedPages[i];
			const uint32 DestinationTileCoord32bit = SparseVolumeTexturePackPageTableEntry(DstTileCoord);

			// Setup the page table entry
			PageTable
				[
					PageCoordToSplat.Z * Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y +
					PageCoordToSplat.Y * Header.PageTableVolumeResolution.X +
					PageCoordToSplat.X
				] = DestinationTileCoord32bit;

			// Set the next tile to be written to
			DstTileCoord = UE::SVT::Private::AdvanceTileCoord(DstTileCoord, TileCoordResolution);
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

			const FIntVector3 WriteTileCoord = SparseVolumeTextureUnpackPageTableEntry(PageTable[PageIndex]);
			const FIntVector3 TileLocalCoord = GridCoord % SPARSE_VOLUME_TILE_RES;
			const FIntVector3 WriteCoord = WriteTileCoord * SPARSE_VOLUME_TILE_RES + TileLocalCoord;
			WriteTileDataVoxel(WriteCoord, AttributesIdx, FVector4f(WriteValue, WriteValue, WriteValue, WriteValue), ComponentIdx);
		});

	return true;
}

uint32 FSparseVolumeRawSource::ReadPageTablePacked(const FIntVector3& PageTableCoord) const
{
	if (PageTableCoord.X < 0 || PageTableCoord.Y < 0 || PageTableCoord.Z < 0
		|| PageTableCoord.X >= Header.PageTableVolumeResolution.X 
		|| PageTableCoord.Y >= Header.PageTableVolumeResolution.Y 
		|| PageTableCoord.Z >= Header.PageTableVolumeResolution.Z)
	{
		return 0;
	}

	const int32 PageIndex = PageTableCoord.Z * (Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.X) + PageTableCoord.Y * Header.PageTableVolumeResolution.X + PageTableCoord.X;
	if (PageTable.IsValidIndex(PageIndex))
	{
		return PageTable[PageIndex];
	}
	return 0;
}

FIntVector3 FSparseVolumeRawSource::ReadPageTable(const FIntVector3& PageTableCoord) const
{
	const uint32 Packed = ReadPageTablePacked(PageTableCoord);
	const FIntVector3 Unpacked = SparseVolumeTextureUnpackPageTableEntry(Packed);
	return Unpacked;
}

FVector4f FSparseVolumeRawSource::ReadTileDataVoxel(const FIntVector3& TileDataCoord, int32 AttributesIdx) const
{
	using namespace UE::SVT::Private;
	check(AttributesIdx >= 0 && AttributesIdx <= 1);
	if ((AttributesIdx == 0 && PhysicalTileDataA.IsEmpty()) || (AttributesIdx == 1 && PhysicalTileDataB.IsEmpty()))
	{
		return FVector4f();
	}

	if (TileDataCoord.X < 0 || TileDataCoord.Y < 0 || TileDataCoord.Z < 0
		|| TileDataCoord.X >= Header.TileDataVolumeResolution.X 
		|| TileDataCoord.Y >= Header.TileDataVolumeResolution.Y 
		|| TileDataCoord.Z >= Header.TileDataVolumeResolution.Z)
	{
		return FVector4f();
	}

	const int32 VoxelIndex = TileDataCoord.Z * (Header.TileDataVolumeResolution.Y * Header.TileDataVolumeResolution.X) + TileDataCoord.Y * Header.TileDataVolumeResolution.X + TileDataCoord.X;
	const uint8* TileData = AttributesIdx == 0 ? PhysicalTileDataA.GetData() : PhysicalTileDataB.GetData();
	const EPixelFormat Format = Header.AttributesFormats[AttributesIdx];

	switch (Format)
	{
	case PF_R8:
		return FVector4f(TileData[VoxelIndex] / 255.0f, 0.0f, 0.0f, 0.0f);
	case PF_R8G8:
		return FVector4f(TileData[VoxelIndex * 2 + 0] / 255.0f, TileData[VoxelIndex * 2 + 1] / 255.0f, 0.0f, 0.0f);
	case PF_R8G8B8A8:
		return FVector4f(TileData[VoxelIndex * 4 + 0] / 255.0f, TileData[VoxelIndex * 4 + 1] / 255.0f, TileData[VoxelIndex * 4 + 2] / 255.0f, TileData[VoxelIndex * 4 + 3] / 255.0f);
	case PF_R16F:
		return FVector4f(F16ToF32(((const uint16*)TileData)[VoxelIndex]), 0.0f, 0.0f, 0.0f);
	case PF_G16R16F:
		return FVector4f(F16ToF32(((const uint16*)TileData)[VoxelIndex * 2 + 0]), F16ToF32(((const uint16*)TileData)[VoxelIndex * 2 + 1]), 0.0f, 0.0f);
	case PF_FloatRGBA:
		return FVector4f(F16ToF32(((const uint16*)TileData)[VoxelIndex * 4 + 0]), F16ToF32(((const uint16*)TileData)[VoxelIndex * 4 + 1]), F16ToF32(((const uint16*)TileData)[VoxelIndex * 4 + 2]), F16ToF32(((const uint16*)TileData)[VoxelIndex * 4 + 3]));
	case PF_R32_FLOAT:
		return FVector4f(((const float*)TileData)[VoxelIndex], 0.0f, 0.0f, 0.0f);
	case PF_G32R32F:
		return FVector4f(((const float*)TileData)[VoxelIndex * 2 + 0], ((const float*)TileData)[VoxelIndex * 2 + 1], 0.0f, 0.0f);
	case PF_A32B32G32R32F:
		return FVector4f(((const float*)TileData)[VoxelIndex * 4 + 0], ((const float*)TileData)[VoxelIndex * 4 + 1], ((const float*)TileData)[VoxelIndex * 4 + 2], ((const float*)TileData)[VoxelIndex * 4 + 3]);
	default:
		checkNoEntry();
		return FVector4f();
	}
}

FVector4f FSparseVolumeRawSource::Sample(const FIntVector3& VolumeCoord, int32 AttributesIdx) const
{
	const FIntVector3 PageTableCoord = (VolumeCoord / SPARSE_VOLUME_TILE_RES) - Header.PageTableVolumeAABBMin;
	const FIntVector3 TileCoord = ReadPageTable(PageTableCoord);
	const FIntVector3 VoxelCoord = (TileCoord * SPARSE_VOLUME_TILE_RES) + (VolumeCoord % SPARSE_VOLUME_TILE_RES);
	const FVector4f Sample = ReadTileDataVoxel(VoxelCoord, AttributesIdx);
	return Sample;
}

void FSparseVolumeRawSource::Sample(const FIntVector3& VolumeCoord, FVector4f& OutAttributesA, FVector4f& OutAttributesB) const
{
	const FIntVector3 PageTableCoord = (VolumeCoord / SPARSE_VOLUME_TILE_RES) - Header.PageTableVolumeAABBMin;
	const FIntVector3 TileCoord = ReadPageTable(PageTableCoord);
	const FIntVector3 VoxelCoord = (TileCoord * SPARSE_VOLUME_TILE_RES) + (VolumeCoord % SPARSE_VOLUME_TILE_RES);
	OutAttributesA = ReadTileDataVoxel(VoxelCoord, 0);
	OutAttributesB = ReadTileDataVoxel(VoxelCoord, 1);
}

void FSparseVolumeRawSource::WriteTileDataVoxel(const FIntVector3& TileDataCoord, int32 AttributesIdx, const FVector4f& Value, int32 DstComponent)
{
	check(AttributesIdx >= 0 && AttributesIdx <= 1);
	if ((AttributesIdx == 0 && PhysicalTileDataA.IsEmpty()) || (AttributesIdx == 1 && PhysicalTileDataB.IsEmpty()))
	{
		return;
	}

	const int32 VoxelIndex = TileDataCoord.Z * (Header.TileDataVolumeResolution.Y * Header.TileDataVolumeResolution.X) + TileDataCoord.Y * Header.TileDataVolumeResolution.X + TileDataCoord.X;
	uint8* TileData = AttributesIdx == 0 ? PhysicalTileDataA.GetData() : PhysicalTileDataB.GetData();
	const EPixelFormat Format = Header.AttributesFormats[AttributesIdx];

	switch (Format)
	{
	case PF_R8:
		if (DstComponent == -1 || DstComponent == 0) TileData[VoxelIndex] = uint8(FMath::Clamp(Value.X, 0.0f, 1.0f) * 255.0f);
		break;
	case PF_R8G8:
		if (DstComponent == -1 || DstComponent == 0) TileData[VoxelIndex * 2 + 0] = uint8(FMath::Clamp(Value.X, 0.0f, 1.0f) * 255.0f);
		if (DstComponent == -1 || DstComponent == 1) TileData[VoxelIndex * 2 + 1] = uint8(FMath::Clamp(Value.Y, 0.0f, 1.0f) * 255.0f);
		break;
	case PF_R8G8B8A8:
		if (DstComponent == -1 || DstComponent == 0) TileData[VoxelIndex * 4 + 0] = uint8(FMath::Clamp(Value.X, 0.0f, 1.0f) * 255.0f);
		if (DstComponent == -1 || DstComponent == 1) TileData[VoxelIndex * 4 + 1] = uint8(FMath::Clamp(Value.Y, 0.0f, 1.0f) * 255.0f);
		if (DstComponent == -1 || DstComponent == 2) TileData[VoxelIndex * 4 + 2] = uint8(FMath::Clamp(Value.Z, 0.0f, 1.0f) * 255.0f);
		if (DstComponent == -1 || DstComponent == 3) TileData[VoxelIndex * 4 + 3] = uint8(FMath::Clamp(Value.W, 0.0f, 1.0f) * 255.0f);
		break;
	case PF_R16F:
		if (DstComponent == -1 || DstComponent == 0) ((uint16*)TileData)[VoxelIndex] = FFloat16(Value.X).Encoded;
		break;
	case PF_G16R16F:
		if (DstComponent == -1 || DstComponent == 0) ((uint16*)TileData)[VoxelIndex * 2 + 0] = FFloat16(Value.X).Encoded;
		if (DstComponent == -1 || DstComponent == 1) ((uint16*)TileData)[VoxelIndex * 2 + 1] = FFloat16(Value.Y).Encoded;
		break;
	case PF_FloatRGBA:
		if (DstComponent == -1 || DstComponent == 0) ((uint16*)TileData)[VoxelIndex * 4 + 0] = FFloat16(Value.X).Encoded;
		if (DstComponent == -1 || DstComponent == 1) ((uint16*)TileData)[VoxelIndex * 4 + 1] = FFloat16(Value.Y).Encoded;
		if (DstComponent == -1 || DstComponent == 2) ((uint16*)TileData)[VoxelIndex * 4 + 2] = FFloat16(Value.Z).Encoded;
		if (DstComponent == -1 || DstComponent == 3) ((uint16*)TileData)[VoxelIndex * 4 + 3] = FFloat16(Value.W).Encoded;
		break;
	case PF_R32_FLOAT:
		if (DstComponent == -1 || DstComponent == 0) ((float*)TileData)[VoxelIndex] = Value.X;
		break;
	case PF_G32R32F:
		if (DstComponent == -1 || DstComponent == 0) ((float*)TileData)[VoxelIndex * 2 + 0] = Value.X;
		if (DstComponent == -1 || DstComponent == 1) ((float*)TileData)[VoxelIndex * 2 + 1] = Value.Y;
		break;
	case PF_A32B32G32R32F:
		if (DstComponent == -1 || DstComponent == 0) ((float*)TileData)[VoxelIndex * 4 + 0] = Value.X;
		if (DstComponent == -1 || DstComponent == 1) ((float*)TileData)[VoxelIndex * 4 + 1] = Value.Y;
		if (DstComponent == -1 || DstComponent == 2) ((float*)TileData)[VoxelIndex * 4 + 2] = Value.Z;
		if (DstComponent == -1 || DstComponent == 3) ((float*)TileData)[VoxelIndex * 4 + 3] = Value.W;
		break;
	default:
		checkNoEntry();
	}
}

void FSparseVolumeRawSource::FillNullTile(const FVector4f& FallbackValueA, const FVector4f& FallbackValueB)
{
	for (int32 AttributesIdx = 0; AttributesIdx < 2; ++AttributesIdx)
	{
		if ((AttributesIdx == 0 && PhysicalTileDataA.IsEmpty()) || (AttributesIdx == 1 && PhysicalTileDataB.IsEmpty()))
		{
			continue;
		}

		const FVector4f& FallbackValue = AttributesIdx == 0 ? FallbackValueA : FallbackValueB;

		for (int32 Z = 0; Z < SPARSE_VOLUME_TILE_RES; ++Z)
		{
			for (int32 Y = 0; Y < SPARSE_VOLUME_TILE_RES; ++Y)
			{
				for (int32 X = 0; X < SPARSE_VOLUME_TILE_RES; ++X)
				{
					const FIntVector3 WriteCoord = FIntVector3(X, Y, Z);
					WriteTileDataVoxel(WriteCoord, AttributesIdx, FallbackValue);
				}
			}
		}
	}
}

FSparseVolumeRawSource FSparseVolumeRawSource::GenerateMipMap() const
{
	FSparseVolumeRawSource Result{};
	Result.Header = Header; // Overwrite values later
	Result.Header.VirtualVolumeAABBMin = Header.VirtualVolumeAABBMin / 2;
	Result.Header.VirtualVolumeAABBMax = FIntVector3(FMath::Max(1, Header.VirtualVolumeAABBMax.X / 2), FMath::Max(1, Header.VirtualVolumeAABBMax.Y / 2), FMath::Max(1, Header.VirtualVolumeAABBMax.Z / 2));
	Result.Header.VirtualVolumeResolution = Result.Header.VirtualVolumeAABBMax - Result.Header.VirtualVolumeAABBMin;
	Result.Header.PageTableVolumeAABBMin = Result.Header.VirtualVolumeAABBMin / SPARSE_VOLUME_TILE_RES;
	Result.Header.PageTableVolumeAABBMax = (Result.Header.VirtualVolumeAABBMax + FIntVector3(SPARSE_VOLUME_TILE_RES - 1)) / SPARSE_VOLUME_TILE_RES;
	Result.Header.PageTableVolumeResolution = Result.Header.PageTableVolumeAABBMax - Result.Header.PageTableVolumeAABBMin;
	Result.Header.MipLevel = Header.MipLevel + 1;
	Result.Header.bHasNullTile = false;

	// Allocate some memory for temp data (worst case)
	TArray<FIntVector3> LinearAllocatedPages;
	LinearAllocatedPages.SetNum(Result.Header.PageTableVolumeResolution.X * Result.Header.PageTableVolumeResolution.Y * Result.Header.PageTableVolumeResolution.Z);

	// Go over each potential page from the source data and push allocate it if it has any data.
	// Otherwise point to the default empty page.
	int32 NumAllocatedPages = 0;
	for (int32_t PageZ = 0; PageZ < Result.Header.PageTableVolumeResolution.Z; ++PageZ)
	{
		for (int32_t PageY = 0; PageY < Result.Header.PageTableVolumeResolution.Y; ++PageY)
		{
			for (int32_t PageX = 0; PageX < Result.Header.PageTableVolumeResolution.X; ++PageX)
			{
				const FIntVector3 PageCoord = FIntVector3(PageX, PageY, PageZ);
				bool bHasAnyData = false;
				for (int32_t OffsetIdx = 0; OffsetIdx < 8; ++OffsetIdx)
				{
					const FIntVector3 Offset = FIntVector3(OffsetIdx, OffsetIdx >> 1, OffsetIdx >> 2) & 1;
					const FIntVector3 ParentPageTableCoord = (Result.Header.PageTableVolumeAABBMin + PageCoord) * 2 + Offset - Header.PageTableVolumeAABBMin;
					const uint32 PageSample = ReadPageTablePacked(ParentPageTableCoord);
					
					if (PageSample != 0 || !Header.bHasNullTile)
					{
						bHasAnyData = true;
					}
				}
				if (bHasAnyData)
				{
					LinearAllocatedPages[NumAllocatedPages] = PageCoord;
					NumAllocatedPages++;
				}
				Result.Header.bHasNullTile |= !bHasAnyData;
			}
		}
	}

	// Compute Page and Tile VolumeResolution from allocated pages
	Result.Header.TileDataVolumeResolution = UE::SVT::Private::ComputeTileDataVolumeResolution(NumAllocatedPages, Result.Header.bHasNullTile);
	const FIntVector3 TileCoordResolution = Result.Header.TileDataVolumeResolution / SPARSE_VOLUME_TILE_RES;

	// Compute byte sizes of formats as NumTotalBytes/NumVoxels. Note that we do this on the data from this object, not the new downsampled one.
	const int32 FormatSizeA = PhysicalTileDataA.Num() / (Header.TileDataVolumeResolution.X * Header.TileDataVolumeResolution.Y * Header.TileDataVolumeResolution.Z);
	const int32 FormatSizeB = PhysicalTileDataB.Num() / (Header.TileDataVolumeResolution.X * Header.TileDataVolumeResolution.Y * Header.TileDataVolumeResolution.Z);

	// Initialize the SparseVolumeTexture page and tile.
	Result.PageTable.SetNumZeroed(Result.Header.PageTableVolumeResolution.X * Result.Header.PageTableVolumeResolution.Y * Result.Header.PageTableVolumeResolution.Z);
	Result.PhysicalTileDataA.SetNumZeroed(Result.Header.TileDataVolumeResolution.X * Result.Header.TileDataVolumeResolution.Y * Result.Header.TileDataVolumeResolution.Z * FormatSizeA);
	Result.PhysicalTileDataB.SetNumZeroed(Result.Header.TileDataVolumeResolution.X * Result.Header.TileDataVolumeResolution.Y * Result.Header.TileDataVolumeResolution.Z * FormatSizeB);

	// Generate page table and tile volume data by splatting the data
	{
		FIntVector3 DstTileCoord = FIntVector3::ZeroValue;

		// Add an empty tile is needed, reserve slot at coord 0
		if (Result.Header.bHasNullTile)
		{
			Result.FillNullTile(ReadTileDataVoxel(FIntVector3(), 0), ReadTileDataVoxel(FIntVector3(), 1));
			
			// PageTable is all cleared to zero, simply skip a tile
			DstTileCoord = UE::SVT::Private::AdvanceTileCoord(DstTileCoord, TileCoordResolution);
		}

		for (int32 i = 0; i < NumAllocatedPages; ++i)
		{
			const FIntVector3 PageCoordToSplat = LinearAllocatedPages[i];
			const uint32 DestinationTileCoord32bit = SparseVolumeTexturePackPageTableEntry(DstTileCoord);

			// Setup the page table entry
			Result.PageTable
				[
					PageCoordToSplat.Z * Result.Header.PageTableVolumeResolution.X * Result.Header.PageTableVolumeResolution.Y +
					PageCoordToSplat.Y * Result.Header.PageTableVolumeResolution.X +
					PageCoordToSplat.X
				] = DestinationTileCoord32bit;

			// Write tile data
			const FIntVector3 ParentVolumeCoordBase = (Result.Header.PageTableVolumeAABBMin + PageCoordToSplat) * SPARSE_VOLUME_TILE_RES * 2;
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
								const FVector4f SampleValue = Sample(SourceCoord, AttributesIdx);
								DownsampledValue += SampleValue;
							}
							DownsampledValue /= 8.0f;

							const FIntVector3 WriteCoord = DstTileCoord * SPARSE_VOLUME_TILE_RES + FIntVector3(X, Y, Z);
							Result.WriteTileDataVoxel(WriteCoord, AttributesIdx, DownsampledValue);
						}
					}
				}
			}

			// Set the next tile to be written to
			DstTileCoord = UE::SVT::Private::AdvanceTileCoord(DstTileCoord, TileCoordResolution);
		}
	}

	return Result;
}

////////////////////////////////////////////////////////////////////////////////////////////////

void FSparseVolumeAssetHeader::Serialize(FArchive& Ar)
{
	Ar << Version;

	if (Version == 0)
	{
		Ar << VirtualVolumeResolution;
		Ar << VirtualVolumeAABBMin;
		Ar << VirtualVolumeAABBMax;
		Ar << PageTableVolumeResolution;
		Ar << PageTableVolumeAABBMin;
		Ar << PageTableVolumeAABBMax;
		Ar << TileDataVolumeResolution;
		UE::SVT::Private::SerializeEnumAs<uint8>(Ar, AttributesFormats[0]);
		UE::SVT::Private::SerializeEnumAs<uint8>(Ar, AttributesFormats[1]);
		Ar << MipLevel;
		Ar << bHasNullTile;
	}
	else
	{
		// FSparseVolumeAssetHeader needs to account for new version
		check(false);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////

void FSparseVolumeTextureRuntime::Serialize(FArchive& Ar)
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
		// FSparseVolumeRawSource needs to account for new version
		check(false);
	}
}

void FSparseVolumeTextureRuntime::SetAsDefaultTexture()
{
	const uint32 VolumeSize = 1;
	PageTable.SetNumZeroed(VolumeSize * VolumeSize * VolumeSize);
	PhysicalTileDataA.SetNumZeroed(VolumeSize * VolumeSize * VolumeSize);
	PhysicalTileDataB.SetNumZeroed(VolumeSize * VolumeSize * VolumeSize);
}

////////////////////////////////////////////////////////////////////////////////////////////////

FSparseVolumeTextureSceneProxy::FSparseVolumeTextureSceneProxy()
	: FRenderResource()
	, SparseVolumeTextureRuntime()
	, PageTableTextureRHI(nullptr)
	, PhysicalTileDataATextureRHI(nullptr)
	, PhysicalTileDataBTextureRHI(nullptr)
{
}

FSparseVolumeTextureSceneProxy::~FSparseVolumeTextureSceneProxy()
{
}

void FSparseVolumeTextureSceneProxy::GetMemorySize(SIZE_T* SizeCPU, SIZE_T* SizeGPU) const
{
	*SizeCPU += sizeof(FSparseVolumeTextureSceneProxy);
	*SizeCPU += SparseVolumeTextureRuntime.PageTable.GetAllocatedSize();
	*SizeCPU += SparseVolumeTextureRuntime.PhysicalTileDataA.GetAllocatedSize();
	*SizeCPU += SparseVolumeTextureRuntime.PhysicalTileDataB.GetAllocatedSize();

#if RHI_ENABLE_RESOURCE_INFO
	FRHIResourceInfo ResourceInfo;
	if (PageTableTextureRHI && PageTableTextureRHI->GetResourceInfo(ResourceInfo))
	{
		*SizeGPU += ResourceInfo.VRamAllocation.AllocationSize;
	}
	if (PhysicalTileDataATextureRHI && PhysicalTileDataATextureRHI->GetResourceInfo(ResourceInfo))
	{
		*SizeGPU += ResourceInfo.VRamAllocation.AllocationSize;
	}
	if (PhysicalTileDataBTextureRHI && PhysicalTileDataBTextureRHI->GetResourceInfo(ResourceInfo))
	{
		*SizeGPU += ResourceInfo.VRamAllocation.AllocationSize;
	}
#endif
}

void FSparseVolumeTextureSceneProxy::InitRHI()
{
	// Page table
	{
		const EPixelFormat PageEntryFormat = PF_R32_UINT;
		const FIntVector3 PageTableVolumeResolution = SparseVolumeTextureRuntime.Header.PageTableVolumeResolution;
		const FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc::Create3D(TEXT("SparseVolumeTexture.PageTable.RHITexture"),
				PageTableVolumeResolution.X, PageTableVolumeResolution.Y, PageTableVolumeResolution.Z, PageEntryFormat)
			.SetFlags(ETextureCreateFlags::ShaderResource);

		PageTableTextureRHI = RHICreateTexture(Desc);

		const int32 FormatSize = GPixelFormats[PageEntryFormat].BlockBytes;
		const FUpdateTextureRegion3D UpdateRegion(0, 0, 0, 0, 0, 0, PageTableVolumeResolution.X, PageTableVolumeResolution.Y, PageTableVolumeResolution.Z);
		RHIUpdateTexture3D(PageTableTextureRHI, 0, UpdateRegion, UpdateRegion.Width * FormatSize, UpdateRegion.Width * UpdateRegion.Height * FormatSize, (const uint8*)SparseVolumeTextureRuntime.PageTable.GetData());
	}

	// Tile data
	{
		const FIntVector3 TileDataVolumeResolution = SparseVolumeTextureRuntime.Header.TileDataVolumeResolution;
		const EPixelFormat VoxelFormatA = SparseVolumeTextureRuntime.Header.AttributesFormats[0];
		const EPixelFormat VoxelFormatB = SparseVolumeTextureRuntime.Header.AttributesFormats[1];
		const FUpdateTextureRegion3D UpdateRegion(0, 0, 0, 0, 0, 0, TileDataVolumeResolution.X, TileDataVolumeResolution.Y, TileDataVolumeResolution.Z);

		// A
		if (VoxelFormatA != PF_Unknown)
		{
			const FRHITextureCreateDesc Desc =
				FRHITextureCreateDesc::Create3D(TEXT("SparseVolumeTexture.PhysicalTileDataA.RHITexture"),
					TileDataVolumeResolution.X, TileDataVolumeResolution.Y, TileDataVolumeResolution.Z, VoxelFormatA)
				.SetFlags(ETextureCreateFlags::ShaderResource);

			PhysicalTileDataATextureRHI = RHICreateTexture(Desc);

			const int32 FormatSize = GPixelFormats[VoxelFormatA].BlockBytes;
			RHIUpdateTexture3D(PhysicalTileDataATextureRHI, 0, UpdateRegion, UpdateRegion.Width * FormatSize, UpdateRegion.Width * UpdateRegion.Height * FormatSize, (const uint8*)SparseVolumeTextureRuntime.PhysicalTileDataA.GetData());
		}

		// B
		if (VoxelFormatB != PF_Unknown)
		{
			const FRHITextureCreateDesc Desc =
				FRHITextureCreateDesc::Create3D(TEXT("SparseVolumeTexture.PhysicalTileDataB.RHITexture"),
					TileDataVolumeResolution.X, TileDataVolumeResolution.Y, TileDataVolumeResolution.Z, VoxelFormatB)
				.SetFlags(ETextureCreateFlags::ShaderResource);

			PhysicalTileDataBTextureRHI = RHICreateTexture(Desc);

			const int32 FormatSize = GPixelFormats[VoxelFormatB].BlockBytes;
			RHIUpdateTexture3D(PhysicalTileDataBTextureRHI, 0, UpdateRegion, UpdateRegion.Width * FormatSize, UpdateRegion.Width * UpdateRegion.Height * FormatSize, (const uint8*)SparseVolumeTextureRuntime.PhysicalTileDataB.GetData());
		}
	}
}

void FSparseVolumeTextureSceneProxy::ReleaseRHI()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////

FSparseVolumeTextureFrame::FSparseVolumeTextureFrame()
	: RuntimeStreamedInData()
	, SparseVolumeTextureSceneProxy()
#if WITH_EDITORONLY_DATA
	, RawData()
#endif
{
}

FSparseVolumeTextureFrame::~FSparseVolumeTextureFrame()
{
}

bool FSparseVolumeTextureFrame::BuildRuntimeData(FSparseVolumeTextureRuntime* OutRuntimeData)
{
#if WITH_EDITORONLY_DATA
	// Check if the virtualized bulk data payload is available now
	if (RawData.HasPayloadData())
	{
		// First, read the source data in from the raw data stored as bulk data
		UE::Serialization::FEditorBulkDataReader RawDataArchiveReader(RawData);
		FSparseVolumeRawSource SparseVolumeRawSource;
		SparseVolumeRawSource.Serialize(RawDataArchiveReader);

		// Then, convert the raw source data to SVT
		OutRuntimeData->Header = SparseVolumeRawSource.Header;
		OutRuntimeData->PageTable = MoveTemp(SparseVolumeRawSource.PageTable);
		OutRuntimeData->PhysicalTileDataA = MoveTemp(SparseVolumeRawSource.PhysicalTileDataA);
		OutRuntimeData->PhysicalTileDataB = MoveTemp(SparseVolumeRawSource.PhysicalTileDataB);

		// Now unload the raw data
		RawData.UnloadData();
		
		return true;
	}
#endif
	return false;
}

void FSparseVolumeTextureFrame::Serialize(FArchive& Ar, UStreamableSparseVolumeTexture* Owner, int32 FrameIndex)
{
	FStripDataFlags StripFlags(Ar);

	const bool bInlinePayload = (FrameIndex == 0);
	RuntimeStreamedInData.SetBulkDataFlags(bInlinePayload ? BULKDATA_ForceInlinePayload : BULKDATA_Force_NOT_InlinePayload);

	if (StripFlags.IsEditorDataStripped() && Ar.IsLoadingFromCookedPackage())
	{
		// In this case we are loading in game with a cooked build so we only need to load the runtime data.

		// Read cooked bulk data from archive
		RuntimeStreamedInData.Serialize(Ar, Owner);

		if (bInlinePayload)
		{
			SparseVolumeTextureSceneProxy = new FSparseVolumeTextureSceneProxy();

			// Create runtime data from cooked bulk data
			{
				FBulkDataReader BulkDataReader(RuntimeStreamedInData);
				SparseVolumeTextureSceneProxy->GetRuntimeData().Serialize(BulkDataReader);
			}

			// The bulk data is no longer needed
			RuntimeStreamedInData.RemoveBulkData();

			// Runtime data is now valid, initialize the render thread proxy
			BeginInitResource(SparseVolumeTextureSceneProxy);
		}
	}
	else if (Ar.IsCooking())
	{
		// We are cooking the game, serialize the asset out.

		FSparseVolumeTextureRuntime RuntimeData;
		const bool bBuiltRuntimeData = BuildRuntimeData(&RuntimeData);
		check(bBuiltRuntimeData); // SVT_TODO: actual error handling

		// Write runtime data into RuntimeStreamedInData
		{
			FBulkDataWriter BulkDataWriter(RuntimeStreamedInData);
			RuntimeData.Serialize(BulkDataWriter);
		}

		// And now write the cooked bulk data to the archive
		RuntimeStreamedInData.Serialize(Ar, Owner);
	}
	else if (!Ar.IsObjectReferenceCollector())
	{
#if WITH_EDITORONLY_DATA
		// When in EDITOR:
		//  - We only serialize raw data 
		//  - The runtime data is fetched/put from/to DDC
		//  - This EditorBulk data do not load the full and huge OpenVDB data. That is only done explicitly later.
		RawData.Serialize(Ar, Owner);
#endif
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////

USparseVolumeTexture::USparseVolumeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FVector4 USparseVolumeTexture::GetUniformParameter(int32 Index) const
{
	const FSparseVolumeTextureSceneProxy* Proxy = GetSparseVolumeTextureSceneProxy();
	if (Proxy)
	{
		const FSparseVolumeAssetHeader& Header = Proxy->GetHeader();
		switch (Index)
		{
		case ESparseVolumeTexture_TileSize:
		{
			return FVector4(float(SPARSE_VOLUME_TILE_RES), 0.0f, 0.0f, 0.0f);
		}
		case ESparseVolumeTexture_PageTableSize:
		{
			return FVector4(Header.PageTableVolumeResolution.X, Header.PageTableVolumeResolution.Y, Header.PageTableVolumeResolution.Z, 0.0f);
		}
		case ESparseVolumeTexture_UVScale: // fallthrough
		case ESparseVolumeTexture_UVBias:
		{
			FVector Scale;
			FVector Bias;
			GetFrameUVScaleBias(&Scale, &Bias);
			return (Index == ESparseVolumeTexture_UVScale) ? FVector4(Scale) : FVector4(Bias);
		}
		default:
		{
			break;
		}
		}
		checkNoEntry();
		return FVector4(ForceInitToZero);
	}

	// 0 while waiting for the proxy
	return FVector4(ForceInitToZero);
}

void USparseVolumeTexture::GetPackedUniforms(FUintVector4& OutPacked0, FUintVector4& OutPacked1) const
{
	const FVector4 TileSize = GetUniformParameter(ESparseVolumeTexture_TileSize);
	const FVector4 PageTableSize = GetUniformParameter(ESparseVolumeTexture_PageTableSize);
	const FVector4 UVScale = GetUniformParameter(ESparseVolumeTexture_UVScale);
	const FVector4 UVBias = GetUniformParameter(ESparseVolumeTexture_UVBias);
	const FUintVector4 PageTableSizeUIMinusOne = FUintVector4(PageTableSize.X - 1, PageTableSize.Y - 1, PageTableSize.Z - 1, 0);

	auto AsUint = [](float X)
	{
		union { float F; uint32 U; } FU = { X };
		return FU.U;
	};

	OutPacked0.X = AsUint((float)UVScale.X);
	OutPacked0.Y = AsUint((float)UVScale.Y);
	OutPacked0.Z = AsUint((float)UVScale.Z);
	OutPacked0.W = (PageTableSizeUIMinusOne.X & 0x7FF) | ((PageTableSizeUIMinusOne.Y & 0x7FF) << 11) | ((PageTableSizeUIMinusOne.Z & 0x3FF) << 22);
	OutPacked1.X = AsUint((float)UVBias.X);
	OutPacked1.Y = AsUint((float)UVBias.Y);
	OutPacked1.Z = AsUint((float)UVBias.Z);
	OutPacked1.W = AsUint((float)TileSize.X);
}

void USparseVolumeTexture::GetFrameUVScaleBias(FVector* OutScale, FVector* OutBias) const
{
	*OutScale = FVector::One();
	*OutBias = FVector::Zero();
	const FSparseVolumeTextureSceneProxy* Proxy = GetSparseVolumeTextureSceneProxy();
	if (Proxy)
	{
		const FSparseVolumeAssetHeader& Header = Proxy->GetHeader();
		const int32 MipFactor = 1 << Header.MipLevel;
		const FVector GlobalVolumeRes = FVector(GetVolumeResolution()) / MipFactor;
		check(GlobalVolumeRes.X > 0.0 && GlobalVolumeRes.Y > 0.0 && GlobalVolumeRes.Z > 0.0);
		const FVector FrameBoundsPaddedMin = FVector(Header.PageTableVolumeAABBMin * SPARSE_VOLUME_TILE_RES); // padded to multiple of page size
		const FVector FrameBoundsPaddedMax = FVector(Header.PageTableVolumeAABBMax * SPARSE_VOLUME_TILE_RES);
		const FVector FramePaddedSize = FrameBoundsPaddedMax - FrameBoundsPaddedMin;

		*OutScale = GlobalVolumeRes / FramePaddedSize; // scale from SVT UV space to frame (padded) local UV space
		*OutBias = -(FrameBoundsPaddedMin / GlobalVolumeRes * *OutScale);
	}
}

UE::Shader::EValueType USparseVolumeTexture::GetUniformParameterType(int32 Index)
{
	switch (Index)
	{
	case ESparseVolumeTexture_TileSize:				return UE::Shader::EValueType::Float1;
	case ESparseVolumeTexture_PageTableSize:		return UE::Shader::EValueType::Float3;
	case ESparseVolumeTexture_UVScale:				return UE::Shader::EValueType::Float3;
	case ESparseVolumeTexture_UVBias:				return UE::Shader::EValueType::Float3;
	default:
		break;
	}
	checkNoEntry();
	return UE::Shader::EValueType::Float4;
}

////////////////////////////////////////////////////////////////////////////////////////////////

UStreamableSparseVolumeTexture::UStreamableSparseVolumeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UStreamableSparseVolumeTexture::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	GenerateOrLoadDDCRuntimeDataAndCreateSceneProxy();
#else
	IStreamingManager::Get().GetSparseVolumeTextureStreamingManager().AddSparseVolumeTexture(this); // GenerateOrLoadDDCRuntimeDataAndCreateSceneProxy() handles this in editor builds
#endif
}

void UStreamableSparseVolumeTexture::FinishDestroy()
{
	Super::FinishDestroy();

	IStreamingManager::Get().GetSparseVolumeTextureStreamingManager().RemoveSparseVolumeTexture(this);
}

void UStreamableSparseVolumeTexture::BeginDestroy()
{
	Super::BeginDestroy();
	for (FSparseVolumeTextureFrameMips& FrameMips : Frames)
	{
		for (FSparseVolumeTextureFrame& Frame : FrameMips)
		{
			if (Frame.SparseVolumeTextureSceneProxy)
			{
				ENQUEUE_RENDER_COMMAND(UStreamableSparseVolumeTexture_DeleteSVTProxy)(
					[Proxy = Frame.SparseVolumeTextureSceneProxy](FRHICommandListImmediate& RHICmdList)
					{
						Proxy->ReleaseResource();
						delete Proxy;
					});
				Frame.SparseVolumeTextureSceneProxy = nullptr;
			}
		}
	}
}

void UStreamableSparseVolumeTexture::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	int32 NumFrames = Frames.Num();
	Ar << NumFrames;
	int32 NumMipLevels = Frames.IsEmpty() ? 0 : Frames[0].Num();
	Ar << NumMipLevels;

	if (Ar.IsLoading())
	{
		Frames.Reset(NumFrames);
		Frames.AddDefaulted(NumFrames);
	}
	for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
	{
		Frames[FrameIndex].SetNum(NumMipLevels);
		for (int32 MipLevel = 0; MipLevel < NumMipLevels; ++MipLevel)
		{
			Frames[FrameIndex][MipLevel].Serialize(Ar, this, FrameIndex);
		}
	}
}

#if WITH_EDITOR
void UStreamableSparseVolumeTexture::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	GenerateOrLoadDDCRuntimeDataAndCreateSceneProxy();
}
#endif // WITH_EDITOR

void UStreamableSparseVolumeTexture::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);
	SIZE_T SizeCPU = sizeof(Frames);
	SIZE_T SizeGPU = 0;
	SizeCPU += Frames.GetAllocatedSize();
	for (const FSparseVolumeTextureFrameMips& FrameMips : Frames)
	{
		SizeCPU += FrameMips.GetAllocatedSize();
		for (const FSparseVolumeTextureFrame& Frame : FrameMips)
		{
			if (Frame.SparseVolumeTextureSceneProxy)
			{
				Frame.SparseVolumeTextureSceneProxy->GetMemorySize(&SizeCPU, &SizeGPU);
			}
		}
	}
	ISparseVolumeTextureStreamingManager& StreamingManager = IStreamingManager::Get().GetSparseVolumeTextureStreamingManager();
	StreamingManager.GetMemorySizeForSparseVolumeTexture(this, &SizeCPU, &SizeGPU);
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(SizeCPU);
	CumulativeResourceSize.AddDedicatedVideoMemoryBytes(SizeGPU);
}

const FSparseVolumeTextureSceneProxy* UStreamableSparseVolumeTexture::GetStreamedFrameProxyOrFallback(int32 FrameIndex, int32 MipLevel) const
{
	ISparseVolumeTextureStreamingManager& StreamingManager = IStreamingManager::Get().GetSparseVolumeTextureStreamingManager();
	const FSparseVolumeTextureSceneProxy* Proxy = StreamingManager.GetSparseVolumeTextureSceneProxy(this, FrameIndex, MipLevel, true);

	int32 FallbackFrameIndex = FrameIndex;
	while (!Proxy)
	{
		FallbackFrameIndex = FallbackFrameIndex > 0 ? (FallbackFrameIndex - 1) : (Frames.Num() - 1);
		if (FallbackFrameIndex == FrameIndex)
		{
			UE_LOG(LogSparseVolumeTexture, Warning, TEXT("Failed to get ANY streamed SparseVolumeTexture frame  SVT: %s, FrameIndex: %i"), *GetName(), FrameIndex);
			return nullptr;
		}
		Proxy = StreamingManager.GetSparseVolumeTextureSceneProxy(this, FallbackFrameIndex, MipLevel, false);
	}

	return Proxy;
}

TArrayView<const FSparseVolumeTextureFrameMips> UStreamableSparseVolumeTexture::GetFrames() const
{
	return Frames;
}

void UStreamableSparseVolumeTexture::GenerateOrLoadDDCRuntimeDataAndCreateSceneProxy()
{
#if WITH_EDITORONLY_DATA
	UE::DerivedData::FRequestOwner DDCRequestOwner(UE::DerivedData::EPriority::Normal);
	{
		UE::DerivedData::FRequestBarrier DDCRequestBarrier(DDCRequestOwner);
		for (FSparseVolumeTextureFrameMips& FrameMips : Frames)
		{
			for (FSparseVolumeTextureFrame& Frame : FrameMips)
			{
				// Release any previously allocated render thread proxy
				if (Frame.SparseVolumeTextureSceneProxy)
				{
					BeginReleaseResource(Frame.SparseVolumeTextureSceneProxy);
				}
				else
				{
					Frame.SparseVolumeTextureSceneProxy = new FSparseVolumeTextureSceneProxy();
				}

				GenerateOrLoadDDCRuntimeDataForFrame(Frame, DDCRequestOwner);
			}
		}
	}

	// Wait for all DDC requests to complete before creating the proxies
	DDCRequestOwner.Wait();

	for (FSparseVolumeTextureFrameMips& FrameMips : Frames)
	{
		for (FSparseVolumeTextureFrame& Frame : FrameMips)
		{
			// Runtime data is now valid, initialize the render thread proxy
			BeginInitResource(Frame.SparseVolumeTextureSceneProxy);
		}
	}

	IStreamingManager::Get().GetSparseVolumeTextureStreamingManager().AddSparseVolumeTexture(this);
#endif
}

void UStreamableSparseVolumeTexture::GenerateOrLoadDDCRuntimeDataForFrame(FSparseVolumeTextureFrame& Frame, UE::DerivedData::FRequestOwner& DDCRequestOwner)
{
#if WITH_EDITORONLY_DATA
	using namespace UE::DerivedData;

	static const FString SparseVolumeTextureDDCVersion = TEXT("381AE2A9-A903-4C8F-8486-891E24D6EC70");	// Bump this if you want to ignore all cached data so far.
	const FString DerivedDataKey = Frame.RawData.GetIdentifier().ToString() + SparseVolumeTextureDDCVersion;

	const FCacheKey Key = ConvertLegacyCacheKey(DerivedDataKey);
	const FSharedString Name = MakeStringView(GetPathName());

	UE::DerivedData::GetCache().GetValue({ {Name, Key} }, DDCRequestOwner,
		[this, &Frame, &DDCRequestOwner](FCacheGetValueResponse&& Response)
		{
			if (Response.Status == EStatus::Ok)
			{
				DDCRequestOwner.LaunchTask(TEXT("UStreamableSparseVolumeTexture_DerivedDataLoad"),
					[this, &Frame, Value = MoveTemp(Response.Value)]()
					{
						FSharedBuffer Data = Value.GetData().Decompress();
						FMemoryReaderView Ar(Data, true /*bIsPersistent*/);
						Frame.SparseVolumeTextureSceneProxy->GetRuntimeData().Serialize(Ar);
					});
			}
			else if (Response.Status == EStatus::Error)
			{
				DDCRequestOwner.LaunchTask(TEXT("UStreamableSparseVolumeTexture_DerivedDataBuild"),
					[this, &Frame, &DDCRequestOwner, Name = Response.Name, Key = Response.Key]()
					{
						FSparseVolumeTextureRuntime& RuntimeData = Frame.SparseVolumeTextureSceneProxy->GetRuntimeData();

						// Check if the virtualized bulk data payload is available now
						if (Frame.RawData.HasPayloadData())
						{
							const bool bSuccess = Frame.BuildRuntimeData(&RuntimeData);
							ensure(bSuccess);
						}
						else
						{
							UE_LOG(LogSparseVolumeTexture, Error, TEXT("SparseVolumeTexture - Raw source data is not available for %s. Using default data."), *GetName());
							RuntimeData.SetAsDefaultTexture();
						}

						// Using a LargeMemoryWriter for serialization since the data can be bigger than 2 GB
						FLargeMemoryWriter LargeMemWriter(0, /*bIsPersistent=*/ true);
						RuntimeData.Serialize(LargeMemWriter);

						int64 UncompressedSize = LargeMemWriter.TotalSize();

						// Since the DDC doesn't support data bigger than 2 GB, we only cache for such uncompressed size.
						constexpr int64 SizeThreshold = 2147483648LL;	// 2GB
						const bool bIsCacheable = UncompressedSize < SizeThreshold;
						if (bIsCacheable)
						{
							FValue Value = FValue::Compress(FSharedBuffer::MakeView(LargeMemWriter.GetView()));
							UE::DerivedData::GetCache().PutValue({ {Name, Key, Value} }, DDCRequestOwner);
						}
						else
						{
							UE_LOG(LogSparseVolumeTexture, Error, TEXT("SparseVolumeTexture - the asset is too large to fit in Derived Data Cache %s"), *GetName());
						}
					});
			}
		});
#endif // WITH_EDITORONLY_DATA
}

////////////////////////////////////////////////////////////////////////////////////////////////

UStaticSparseVolumeTexture::UStaticSparseVolumeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////

UAnimatedSparseVolumeTexture::UAnimatedSparseVolumeTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

const FSparseVolumeTextureSceneProxy* UAnimatedSparseVolumeTexture::GetSparseVolumeTextureSceneProxy() const
{
	// When an AnimatedSparseVolumeTexture is used as SparseVolumeTexture, it can only be previewed using a single preview frame.
	check(!Frames.IsEmpty());
	const int32 FrameIndex = PreviewFrameIndex % Frames.Num();
	const int32 MipLevel = FMath::Clamp(PreviewMipLevel, 0, GetNumMipLevels() - 1);
	return GetStreamedFrameProxyOrFallback(FrameIndex, MipLevel);
}

const FSparseVolumeTextureSceneProxy* UAnimatedSparseVolumeTexture::GetSparseVolumeTextureFrameSceneProxy(int32 FrameIndex, int32 MipLevel) const
{
	check(!Frames.IsEmpty());
	FrameIndex = FrameIndex % Frames.Num();
	MipLevel = FMath::Clamp(MipLevel, 0, GetNumMipLevels() - 1);
	return GetStreamedFrameProxyOrFallback(FrameIndex, MipLevel);
}

const FSparseVolumeAssetHeader* UAnimatedSparseVolumeTexture::GetSparseVolumeTextureFrameHeader(int32 FrameIndex, int32 MipLevel) const
{
	check(!Frames.IsEmpty());
	FrameIndex = FrameIndex % Frames.Num();
	MipLevel = FMath::Clamp(MipLevel, 0, GetNumMipLevels() - 1);
	const FSparseVolumeTextureSceneProxy* Proxy = GetStreamedFrameProxyOrFallback(FrameIndex, MipLevel);
	return Proxy ? &Proxy->GetHeader() : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////

USparseVolumeTextureFrame::USparseVolumeTextureFrame(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

USparseVolumeTextureFrame* USparseVolumeTextureFrame::CreateFrame(USparseVolumeTexture* Texture, int32 FrameIndex, int32 MipLevel)
{
	if (!Texture)
	{
		return nullptr;
	}
	
	const FSparseVolumeTextureSceneProxy* Proxy = nullptr;
	if (Texture->IsA<UStreamableSparseVolumeTexture>())
	{
		UStreamableSparseVolumeTexture* StreamableSVT = CastChecked<UStreamableSparseVolumeTexture>(Texture);
		check(StreamableSVT);
		Proxy = StreamableSVT->GetStreamedFrameProxyOrFallback(FrameIndex, MipLevel);
	}
	else
	{
		Proxy = Texture->GetSparseVolumeTextureSceneProxy();
	}

	if (Proxy)
	{
		USparseVolumeTextureFrame* Frame = NewObject<USparseVolumeTextureFrame>();
		Frame->Initialize(Proxy, Texture->GetVolumeResolution());
		return Frame;
	}
	
	return nullptr;
}

void USparseVolumeTextureFrame::Initialize(const FSparseVolumeTextureSceneProxy* InSceneProxy, const FIntVector& InVolumeResolution)
{
	SceneProxy = InSceneProxy;
	VolumeResolution = InVolumeResolution;
}

////////////////////////////////////////////////////////////////////////////////////////////////

UAnimatedSparseVolumeTextureController::UAnimatedSparseVolumeTextureController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UAnimatedSparseVolumeTextureController::Play()
{
	bIsPlaying = true;
}

void UAnimatedSparseVolumeTextureController::Pause()
{
	bIsPlaying = false;
}

void UAnimatedSparseVolumeTextureController::Stop()
{
	if (bIsPlaying)
	{
		bIsPlaying = false;
		Time = 0.0f;
	}
}

bool UAnimatedSparseVolumeTextureController::IsPlaying()
{
	return bIsPlaying;
}

void UAnimatedSparseVolumeTextureController::Update(float DeltaTime)
{
	if (!SparseVolumeTexture || !bIsPlaying)
	{
		return;
	}

	// Update animation time
	const float AnimationDuration = GetDuration();
	Time = FMath::Fmod(Time + DeltaTime, AnimationDuration + UE_SMALL_NUMBER);
}

void UAnimatedSparseVolumeTextureController::SetSparseVolumeTexture(USparseVolumeTexture* Texture)
{
	if (Texture == SparseVolumeTexture)
	{
		return;
	}

	SparseVolumeTexture = Texture;
	bIsPlaying = bIsPlaying && (SparseVolumeTexture != nullptr);
	Time = 0.0f;
}

void UAnimatedSparseVolumeTextureController::SetTime(float InTime)
{
	const float AnimationDuration = GetDuration();
	Time = FMath::Fmod(InTime, AnimationDuration + UE_SMALL_NUMBER);
}

void UAnimatedSparseVolumeTextureController::SetFractionalFrameIndex(float Frame)
{
	if (!SparseVolumeTexture)
	{
		return;
	}

	const int32 FrameCount = SparseVolumeTexture->GetNumFrames();
	Frame = FMath::Fmod(Frame, (float)FrameCount);
	Time = Frame / (FrameRate + UE_SMALL_NUMBER);
}

USparseVolumeTexture* UAnimatedSparseVolumeTextureController::GetSparseVolumeTexture()
{
	return SparseVolumeTexture;
}

float UAnimatedSparseVolumeTextureController::GetTime()
{
	return Time;
}

float UAnimatedSparseVolumeTextureController::GetFractionalFrameIndex()
{
	if (!SparseVolumeTexture)
	{
		return 0.0f;
	}

	const int32 FrameCount = SparseVolumeTexture->GetNumFrames();
	const float FrameIndexF = FMath::Fmod(Time * FrameRate, (float)FrameCount);
	return FrameIndexF;
}

USparseVolumeTextureFrame* UAnimatedSparseVolumeTextureController::GetCurrentFrame()
{
	if (!SparseVolumeTexture)
	{
		return nullptr;
	}

	// Compute (fractional) index of frame to sample
	const float FrameIndexF = GetFractionalFrameIndex();
	const int32 FrameIndex = (int32)FrameIndexF;

	// Create and initialize a USparseVolumeTextureFrame which holds the frame to sample and can be bound to shaders
	USparseVolumeTextureFrame* Frame = USparseVolumeTextureFrame::CreateFrame(SparseVolumeTexture, FrameIndex, MipLevel);

	return Frame;
}

void UAnimatedSparseVolumeTextureController::GetLerpFrames(USparseVolumeTextureFrame*& Frame0, USparseVolumeTextureFrame*& Frame1, float& LerpAlpha)
{
	if (!SparseVolumeTexture)
	{
		return;
	}

	// Compute (fractional) index of frame to sample
	const float FrameIndexF = GetFractionalFrameIndex();
	const int32 FrameIndex = (int32)FrameIndexF;
	LerpAlpha = FMath::Frac(FrameIndexF);

	// Create and initialize a USparseVolumeTextureFrame which holds the frame to sample and can be bound to shaders
	Frame0 = USparseVolumeTextureFrame::CreateFrame(SparseVolumeTexture, FrameIndex, MipLevel);
	Frame1 = USparseVolumeTextureFrame::CreateFrame(SparseVolumeTexture, FrameIndex + 1, MipLevel);
}

float UAnimatedSparseVolumeTextureController::GetDuration()
{
	if (!SparseVolumeTexture)
	{
		return 0.0f;
	}

	const int32 FrameCount = SparseVolumeTexture->GetNumFrames();
	const float AnimationDuration = FrameCount / (FrameRate + UE_SMALL_NUMBER);
	return AnimationDuration;
}

////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
