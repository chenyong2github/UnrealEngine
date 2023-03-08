// Copyright Epic Games, Inc. All Rights Reserved.

#include "SparseVolumeTexture/SparseVolumeTextureSceneProxy.h"
#include "SparseVolumeTexture/SparseVolumeTextureUtility.h"
#include "SparseVolumeTexture/SparseVolumeTextureData.h"

DEFINE_LOG_CATEGORY_STATIC(LogSparseVolumeTextureProxy, Log, All);

namespace UE
{
namespace SVT
{
namespace Private
{
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
	}

	static FIntVector3 ComputeTileDataVolumeResolution(int32 NumAllocatedPages)
	{
		int32 TileVolumeResolutionCube = 1;
		while (TileVolumeResolutionCube * TileVolumeResolutionCube * TileVolumeResolutionCube < NumAllocatedPages)
		{
			TileVolumeResolutionCube++;				// We use a simple loop to compute the minimum resolution of a cube to store all the tile data
		}
		FIntVector3 TileDataVolumeResolution = FIntVector3(TileVolumeResolutionCube, TileVolumeResolutionCube, TileVolumeResolutionCube);
		while (TileDataVolumeResolution.X * TileDataVolumeResolution.Y * (TileDataVolumeResolution.Z - 1) > NumAllocatedPages)
		{
			TileDataVolumeResolution.Z--;	// We then trim an edge to get back space.
		}

		return TileDataVolumeResolution * SPARSE_VOLUME_TILE_RES_PADDED;
	}
} // Private
} // SVT
} // UE

////////////////////////////////////////////////////////////////////////////////////////////////

bool FSparseVolumeTextureRuntime::Create(const FSparseVolumeTextureData& TextureData)
{
	bool bSuccess = Create(TextureData.Header, TextureData.Header.MipInfo.Num());

	if (!bSuccess)
	{
		return false;
	}

	const int32 NumMipLevels = TextureData.Header.MipInfo.Num();
	const int32 TileSizeBytesA = GPixelFormats[Header.AttributesFormats[0]].BlockBytes * UE::SVT::SVTNumVoxelsPerPaddedTile;
	const int32 TileSizeBytesB = GPixelFormats[Header.AttributesFormats[1]].BlockBytes * UE::SVT::SVTNumVoxelsPerPaddedTile;

	TArray<FSparseVolumeTextureTileMapping> Mappings;
	Mappings.Empty(NumMipLevels);
	for (int32 MipLevel = 0; MipLevel < NumMipLevels; ++MipLevel)
	{
		FSparseVolumeTextureTileMapping Mapping;
		Mapping.TileIndices = TextureData.PageTable[MipLevel].GetData();
		Mapping.TileDataA = TextureData.PhysicalTileDataA.GetData() + TileSizeBytesA * TextureData.Header.MipInfo[MipLevel].TileOffset;
		Mapping.TileDataB = TextureData.PhysicalTileDataB.GetData() + TileSizeBytesB * TextureData.Header.MipInfo[MipLevel].TileOffset;
		Mapping.NumPhysicalTiles = TextureData.Header.MipInfo[MipLevel].TileCount;
		Mapping.TileIndicesOffset = -TextureData.Header.MipInfo[MipLevel].TileOffset;

		Mappings.Add(Mapping);
	}

	return SetTileMappings(Mappings);
}

bool FSparseVolumeTextureRuntime::Create(const FSparseVolumeTextureHeader& SVTHeader, int32 NumMipLevels)
{
	using namespace UE::SVT;

	// Check if the requested resolution exceeds hardware limits
	if (SVTHeader.PageTableVolumeResolution.X > SVTMaxVolumeTextureDim
		|| SVTHeader.PageTableVolumeResolution.Y > SVTMaxVolumeTextureDim
		|| SVTHeader.PageTableVolumeResolution.Z > SVTMaxVolumeTextureDim)
	{
		UE_LOG(LogSparseVolumeTextureProxy, Warning, TEXT("FSparseVolumeTextureRuntime page table texture dimensions exceed limit (%ix%ix%i): %ix%ix%i"),
			SVTMaxVolumeTextureDim, SVTMaxVolumeTextureDim, SVTMaxVolumeTextureDim,
			SVTHeader.PageTableVolumeResolution.X, SVTHeader.PageTableVolumeResolution.Y, SVTHeader.PageTableVolumeResolution.Z);
		return false;
	}

	// Check if number of requested mip levels are possible
	{
		int32 PossibleLevels = 1;
		FIntVector3 Resolution = SVTHeader.VirtualVolumeResolution;
		while (Resolution.X > SPARSE_VOLUME_TILE_RES || Resolution.Y > SPARSE_VOLUME_TILE_RES || Resolution.Z > SPARSE_VOLUME_TILE_RES)
		{
			Resolution /= 2;
			++PossibleLevels;
		}
		if (NumMipLevels <= 0 || NumMipLevels > PossibleLevels)
		{
			UE_LOG(LogSparseVolumeTextureProxy, Warning, TEXT("Tried to create FSparseVolumeTextureRuntime with more mip levels than possible. Requested %i Possible %i"), NumMipLevels, PossibleLevels);
			return false;
		}
	}

	// Check if formats are supported
	if (SVTHeader.AttributesFormats[0] == PF_Unknown && SVTHeader.AttributesFormats[1] == PF_Unknown)
	{
		UE_LOG(LogSparseVolumeTextureProxy, Warning, TEXT("Tried to create FSparseVolumeTextureRuntime with pixel format == PF_Unknown for both attribute textures."));
		return false;
	}
	for (int32 i = 0; i < 2; ++i)
	{
		EPixelFormat Format = SVTHeader.AttributesFormats[i];
		switch (Format)
		{
		case PF_R8:
		case PF_R8G8:
		case PF_R8G8B8A8:
		case PF_R16F:
		case PF_G16R16F:
		case PF_FloatRGBA:
		case PF_R32_FLOAT:
		case PF_G32R32F:
		case PF_A32B32G32R32F:
		case PF_Unknown: // Allow PF_Unknown because the above check guarantees that at least one format is not PF_Unknown.
			break;
		default:
			UE_LOG(LogSparseVolumeTextureProxy, Warning, TEXT("Tried to create FSparseVolumeTextureRuntime with unsupported pixel format. Requested %i"), Format);
			return false;
		}
	}

	*static_cast<FSparseVolumeTextureHeader*>(&Header) = SVTHeader;
	Header.TileDataVolumeResolution = FIntVector3::ZeroValue;
	Header.NumMipLevels = NumMipLevels;
	Header.HighestResidentLevel = INT32_MIN;
	Header.LowestResidentLevel = INT32_MAX;
	PageTable.SetNum(NumMipLevels);
	PhysicalTileDataA.Reset();
	PhysicalTileDataB.Reset();

	for (int32 MipLevel = 0; MipLevel < NumMipLevels; ++MipLevel)
	{
		PageTable[MipLevel].Empty();
	}

	return true;
}

bool FSparseVolumeTextureRuntime::SetTileMappings(const TArrayView<const FSparseVolumeTextureTileMapping>& Mappings)
{
	using namespace UE::SVT;
	using namespace UE::SVT::Private;

	const int32 NumMipLevels = Header.NumMipLevels;
	check(Mappings.Num() == NumMipLevels)

		// Compute number of required tiles
		int32 NumTiles = 1; // always need a null tile
	for (const FSparseVolumeTextureTileMapping& Mapping : Mappings)
	{
		NumTiles += Mapping.NumPhysicalTiles;
	}

	FIntVector3 TileDataVolumeRes = ComputeTileDataVolumeResolution(NumTiles);
	if (TileDataVolumeRes.X > SVTMaxVolumeTextureDim
		|| TileDataVolumeRes.Y > SVTMaxVolumeTextureDim
		|| TileDataVolumeRes.Z > SVTMaxVolumeTextureDim)
	{
		UE_LOG(LogSparseVolumeTextureProxy, Warning, TEXT("SparseVolumeTexture tile data texture dimensions exceed limit (%ix%ix%i): %ix%ix%i"),
			SVTMaxVolumeTextureDim, SVTMaxVolumeTextureDim, SVTMaxVolumeTextureDim,
			TileDataVolumeRes.X, TileDataVolumeRes.Y, TileDataVolumeRes.Z);
		return false;
	}

	Header.TileDataVolumeResolution = TileDataVolumeRes;
	const FIntVector3 TileCoordSpace = TileDataVolumeRes / SPARSE_VOLUME_TILE_RES_PADDED;
	check((TileCoordSpace.X * SPARSE_VOLUME_TILE_RES_PADDED) == TileDataVolumeRes.X
		&& (TileCoordSpace.Y * SPARSE_VOLUME_TILE_RES_PADDED) == TileDataVolumeRes.Y
		&& (TileCoordSpace.Z * SPARSE_VOLUME_TILE_RES_PADDED) == TileDataVolumeRes.Z);

	// Clear page table
	for (TArray<uint32>& PageTableMip : PageTable)
	{
		PageTableMip.Empty();
	}

	// Allocate memory for tile data
	const int32 FormatSize[] = { GPixelFormats[Header.AttributesFormats[0]].BlockBytes, GPixelFormats[Header.AttributesFormats[1]].BlockBytes };
	PhysicalTileDataA.SetNumZeroed(Header.TileDataVolumeResolution.X * Header.TileDataVolumeResolution.Y * Header.TileDataVolumeResolution.Z * FormatSize[0]);
	PhysicalTileDataB.SetNumZeroed(Header.TileDataVolumeResolution.X * Header.TileDataVolumeResolution.Y * Header.TileDataVolumeResolution.Z * FormatSize[1]);
	uint8* PhysicalTileDataPtrs[] = { PhysicalTileDataA.GetData(), PhysicalTileDataB.GetData() };

	int32 NumWrittenTiles = 0;
	FIntVector3 DstTileCoord = FIntVector3::ZeroValue;

	// Write null tile
	for (int32 Z = 0; Z < SPARSE_VOLUME_TILE_RES_PADDED; ++Z)
	{
		for (int32 Y = 0; Y < SPARSE_VOLUME_TILE_RES_PADDED; ++Y)
		{
			for (int32 X = 0; X < SPARSE_VOLUME_TILE_RES_PADDED; ++X)
			{
				const int32 VoxelIndex = Z * (SPARSE_VOLUME_TILE_RES_PADDED * SPARSE_VOLUME_TILE_RES_PADDED) + Y * SPARSE_VOLUME_TILE_RES_PADDED + X;
				WriteVoxel(VoxelIndex, PhysicalTileDataPtrs[0], Header.AttributesFormats[0], Header.NullTileValues[0]);
				WriteVoxel(VoxelIndex, PhysicalTileDataPtrs[1], Header.AttributesFormats[1], Header.NullTileValues[1]);
			}
		}
	}
	++NumWrittenTiles;
	DstTileCoord = AdvanceTileCoord(DstTileCoord, TileCoordSpace);

	// Write page table and physical tiles
	Header.HighestResidentLevel = INT32_MIN;
	Header.LowestResidentLevel = INT32_MAX;
	FIntVector3 PageTableRes = Header.PageTableVolumeResolution * 2;
	for (int32 MipLevel = 0; MipLevel < NumMipLevels; ++MipLevel)
	{
		PageTableRes = FIntVector3(FMath::Max(1, PageTableRes.X / 2), FMath::Max(1, PageTableRes.Y / 2), FMath::Max(1, PageTableRes.Z / 2));

		const FSparseVolumeTextureTileMapping& Mapping = Mappings[MipLevel];
		if (Mapping.NumPhysicalTiles <= 0)
		{
			continue;
		}

		Header.HighestResidentLevel = FMath::Max(Header.HighestResidentLevel, MipLevel);
		Header.LowestResidentLevel = FMath::Min(Header.LowestResidentLevel, MipLevel);

		// Write page table
		PageTable[MipLevel].SetNum(PageTableRes.X * PageTableRes.Y * PageTableRes.Z);
		uint32* PageTablePtr = PageTable[MipLevel].GetData();
		const int32 MipTileOffset = NumWrittenTiles;
		for (int32 PageZ = 0; PageZ < PageTableRes.Z; ++PageZ)
		{
			for (int32 PageY = 0; PageY < PageTableRes.Y; ++PageY)
			{
				for (int32 PageX = 0; PageX < PageTableRes.X; ++PageX)
				{
					const int32 PageIndex = PageZ * (PageTableRes.Y * PageTableRes.X) + PageY * PageTableRes.X + PageX;
					uint32 TileIndex = Mapping.TileIndices[PageIndex];
					if (TileIndex == 0) // points to null tile
					{
						PageTablePtr[PageIndex] = 0;
					}
					else // points to actual physical tile
					{
						// Make index relative to the start index of this mip level
						TileIndex += Mapping.TileIndicesOffset;
						TileIndex += MipTileOffset;

						FIntVector3 TileCoord;
						TileCoord.X = TileIndex % TileCoordSpace.X;
						TileCoord.Z = TileIndex / (TileCoordSpace.Y * TileCoordSpace.X);
						TileCoord.Y = (TileIndex - (TileCoord.Z * (TileCoordSpace.Y * TileCoordSpace.X))) / TileCoordSpace.X;

						// Write to page table
						PageTablePtr[PageIndex] = PackPageTableEntry(TileCoord);
					}
				}
			}
		}

		// Write to tile data
		for (int32 PhysicalTileIndex = 0; PhysicalTileIndex < Mapping.NumPhysicalTiles; ++PhysicalTileIndex)
		{
			for (int32 Z = 0; Z < SPARSE_VOLUME_TILE_RES_PADDED; ++Z)
			{
				for (int32 Y = 0; Y < SPARSE_VOLUME_TILE_RES_PADDED; ++Y)
				{
					for (int32 X = 0; X < SPARSE_VOLUME_TILE_RES_PADDED; ++X)
					{
						const int32 SrcVoxelIndex = PhysicalTileIndex * SVTNumVoxelsPerPaddedTile + Z * (SPARSE_VOLUME_TILE_RES_PADDED * SPARSE_VOLUME_TILE_RES_PADDED) + Y * SPARSE_VOLUME_TILE_RES_PADDED + X;
						const FVector4f ValueA = ReadVoxel(SrcVoxelIndex, Mapping.TileDataA, Header.AttributesFormats[0]);
						const FVector4f ValueB = ReadVoxel(SrcVoxelIndex, Mapping.TileDataB, Header.AttributesFormats[1]);

						const FIntVector3 VoxelCoord = DstTileCoord * SPARSE_VOLUME_TILE_RES_PADDED + FIntVector3(X, Y, Z);
						const int32 DstVoxelIndex = VoxelCoord.Z * (Header.TileDataVolumeResolution.Y * Header.TileDataVolumeResolution.X) + VoxelCoord.Y * Header.TileDataVolumeResolution.X + VoxelCoord.X;
						WriteVoxel(DstVoxelIndex, PhysicalTileDataPtrs[0], Header.AttributesFormats[0], ValueA);
						WriteVoxel(DstVoxelIndex, PhysicalTileDataPtrs[1], Header.AttributesFormats[1], ValueB);
					}
				}
			}
			DstTileCoord = AdvanceTileCoord(DstTileCoord, TileCoordSpace);
		}
		NumWrittenTiles += Mapping.NumPhysicalTiles;
	}
	return true;
}

void FSparseVolumeTextureRuntime::SetAsDefaultTexture()
{
	Header.PageTableVolumeResolution = FIntVector3(1, 1, 1);
	Header.TileDataVolumeResolution = FIntVector3(1, 1, 1);
	Header.NumMipLevels = 1;
	Header.HighestResidentLevel = 0;
	Header.LowestResidentLevel = 0;
	PageTable.SetNum(1);
	PageTable[0].SetNumZeroed(1);
	const int32 FormatSize[] = { GPixelFormats[Header.AttributesFormats[0]].BlockBytes, GPixelFormats[Header.AttributesFormats[1]].BlockBytes };
	PhysicalTileDataA.SetNumZeroed(FormatSize[0]);
	PhysicalTileDataB.SetNumZeroed(FormatSize[1]);
	UE::SVT::WriteVoxel(0, PhysicalTileDataA.GetData(), Header.AttributesFormats[0], Header.NullTileValues[0]);
	UE::SVT::WriteVoxel(0, PhysicalTileDataB.GetData(), Header.AttributesFormats[1], Header.NullTileValues[1]);
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
	// Can't create a proxy without any resident mips
	check(SparseVolumeTextureRuntime.Header.HighestResidentLevel >= SparseVolumeTextureRuntime.Header.LowestResidentLevel);

	// Page table
	{
		const int32 NumResidentMipLevels = SparseVolumeTextureRuntime.Header.HighestResidentLevel - SparseVolumeTextureRuntime.Header.LowestResidentLevel + 1;
		FIntVector3 PageTableResolution = SparseVolumeTextureRuntime.Header.PageTableVolumeResolution / (1 << SparseVolumeTextureRuntime.Header.LowestResidentLevel);
		PageTableResolution = FIntVector3(FMath::Max(1, PageTableResolution.X), FMath::Max(1, PageTableResolution.Y), FMath::Max(1, PageTableResolution.Z));

		const EPixelFormat PageEntryFormat = PF_R32_UINT;
		const FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc::Create3D(TEXT("SparseVolumeTexture.PageTable.RHITexture"), PageTableResolution.X, PageTableResolution.Y, PageTableResolution.Z, PageEntryFormat)
			.SetFlags(ETextureCreateFlags::ShaderResource)
			.SetNumMips((uint8)NumResidentMipLevels);

		PageTableTextureRHI = RHICreateTexture(Desc);

		const int32 FormatSize = GPixelFormats[PageEntryFormat].BlockBytes;
		FIntVector3 PageTableMipRes = PageTableResolution;
		for (int32 RelativeMipLevel = 0; RelativeMipLevel < NumResidentMipLevels; ++RelativeMipLevel)
		{
			const FUpdateTextureRegion3D UpdateRegion(0, 0, 0, 0, 0, 0, PageTableMipRes.X, PageTableMipRes.Y, PageTableMipRes.Z);
			const uint8* PageTableData = (const uint8*)SparseVolumeTextureRuntime.PageTable[RelativeMipLevel + SparseVolumeTextureRuntime.Header.LowestResidentLevel].GetData();
			RHIUpdateTexture3D(PageTableTextureRHI, (uint32)RelativeMipLevel, UpdateRegion, UpdateRegion.Width * FormatSize, UpdateRegion.Width * UpdateRegion.Height * FormatSize, PageTableData);
			PageTableMipRes = FIntVector3(FMath::Max(1, PageTableMipRes.X / 2), FMath::Max(1, PageTableMipRes.Y / 2), FMath::Max(1, PageTableMipRes.Z / 2));
		}
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
				FRHITextureCreateDesc::Create3D(TEXT("SparseVolumeTexture.PhysicalTileDataA.RHITexture"), TileDataVolumeResolution.X, TileDataVolumeResolution.Y, TileDataVolumeResolution.Z, VoxelFormatA)
				.SetFlags(ETextureCreateFlags::ShaderResource);

			PhysicalTileDataATextureRHI = RHICreateTexture(Desc);

			const int32 FormatSize = GPixelFormats[VoxelFormatA].BlockBytes;
			RHIUpdateTexture3D(PhysicalTileDataATextureRHI, 0, UpdateRegion, UpdateRegion.Width * FormatSize, UpdateRegion.Width * UpdateRegion.Height * FormatSize, (const uint8*)SparseVolumeTextureRuntime.PhysicalTileDataA.GetData());
		}

		// B
		if (VoxelFormatB != PF_Unknown)
		{
			const FRHITextureCreateDesc Desc =
				FRHITextureCreateDesc::Create3D(TEXT("SparseVolumeTexture.PhysicalTileDataB.RHITexture"), TileDataVolumeResolution.X, TileDataVolumeResolution.Y, TileDataVolumeResolution.Z, VoxelFormatB)
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