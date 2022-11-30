// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "SparseVolumeTextureOpenVDBUtility.h"
#include "SparseVolumeTextureOpenVDB.h"
#include "SparseVolumeTexture/SparseVolumeTexture.h"

DEFINE_LOG_CATEGORY_STATIC(LogSparseVolumeTextureOpenVDBUtility, Log, All);

#if OPENVDB_AVAILABLE

namespace
{
	// Utility class acting as adapter between TArray<uint8> and std::istream
	class FArrayUint8StreamBuf : public std::streambuf
	{
	public:
		explicit FArrayUint8StreamBuf(TArray<uint8>& Array)
		{
			char* Data = (char*)Array.GetData();
			setg(Data, Data, Data + Array.Num());
		}
	};
}

static FOpenVDBData GetOpenVDBData(openvdb::GridBase::Ptr GridBase)
{
	FOpenVDBData OpenVDBData;
	openvdb::CoordBBox VolumeActiveAABB = GridBase->evalActiveVoxelBoundingBox();
	openvdb::Coord VolumeActiveDim = GridBase->evalActiveVoxelDim();
	openvdb::Vec3d VolumeVoxelSize = GridBase->voxelSize();

	OpenVDBData.VolumeActiveAABBMin = FVector(VolumeActiveAABB.min().x(), VolumeActiveAABB.min().y(), VolumeActiveAABB.min().z());
	OpenVDBData.VolumeActiveAABBMax = FVector(VolumeActiveAABB.max().x(), VolumeActiveAABB.max().y(), VolumeActiveAABB.max().z());
	OpenVDBData.VolumeActiveDim = FVector(VolumeActiveDim.x(), VolumeActiveDim.y(), VolumeActiveDim.z());
	OpenVDBData.VolumeVoxelSize = FVector(VolumeVoxelSize.x(), VolumeVoxelSize.y(), VolumeVoxelSize.z());
	OpenVDBData.bIsInWorldSpace = GridBase->isInWorldSpace();
	OpenVDBData.bHasUniformVoxels = GridBase->hasUniformVoxels();

	return OpenVDBData;
}

#endif

bool IsOpenVDBDataValid(FOpenVDBData& OpenVDBData, const FString& Filename)
{
	if (OpenVDBData.VolumeActiveDim.X * OpenVDBData.VolumeActiveDim.Y * OpenVDBData.VolumeActiveDim.Z == 0)
	{
		// SVT_TODO we should gently handle that case
		UE_LOG(LogSparseVolumeTextureOpenVDBUtility, Error, TEXT("OpenVDB asset is empty due to volume size being 0: %s"), *Filename);
		return false;
	}

	if (!OpenVDBData.bHasUniformVoxels)
	{
		UE_LOG(LogSparseVolumeTextureOpenVDBUtility, Error, TEXT("OpenVDB importer cannot handle non uniform voxels: %s"), *Filename);
		return false;
	}
	return true;
}

bool FindDensityGridIndex(TArray<uint8>& SourceFile, const FString& Filename, uint32* OutGridIndex, FOpenVDBData* OutOVDBData)
{
#if OPENVDB_AVAILABLE
	FArrayUint8StreamBuf StreamBuf(SourceFile);
	std::istream IStream(&StreamBuf);
	openvdb::io::Stream Stream(IStream, false /*delayLoad*/);

	openvdb::GridPtrVecPtr Grids = Stream.getGrids();

	if (!Grids)
	{
		UE_LOG(LogSparseVolumeTextureOpenVDBUtility, Warning, TEXT("OpenVDB file contains no grids: %s"), *Filename);
		return false;
	}

	uint32 GridIndex = 0;
	openvdb::GridBase::Ptr BaseGrid = nullptr;
	for (openvdb::GridBase::Ptr& Grid : *Grids)
	{
		if (Grid->getName() == "density")
		{
			if (Grid->isType<openvdb::FloatGrid>())
			{
				BaseGrid = Grid;
				break;
			}
			break;
		}
		++GridIndex;
	}

	// If we have not found any density map, let's pick up the first float map
	if (!BaseGrid)
	{
		GridIndex = 0;
		for (openvdb::GridBase::Ptr& Grid : *Grids)
		{
			if (Grid->isType<openvdb::FloatGrid>())
			{
				BaseGrid = Grid;
				break;
			}
			++GridIndex;
		}
	}

	if (BaseGrid)
	{
		openvdb::FloatGrid::Ptr DensityGrid = openvdb::gridPtrCast<openvdb::FloatGrid>(BaseGrid);

		// Only open float grid for now
		if (DensityGrid == nullptr)
		{
			UE_LOG(LogSparseVolumeTextureOpenVDBUtility, Error, TEXT("Could not convert the grid to float: %s"), *Filename);
			return false;
		}

		FOpenVDBData OVDBData = GetOpenVDBData(DensityGrid);
		if (!IsOpenVDBDataValid(OVDBData, Filename))
		{
			return false;
		}

		*OutGridIndex = GridIndex;
		*OutOVDBData = OVDBData;
		return true;
	}

#endif // OPENVDB_AVAILABLE

	return false;
}

bool GetOpenVDBGridInfo(TArray<uint8>& SourceFile, const FString& Filename, TArray<FOpenVDBGridInfo>* OutGridInfo)
{
#if OPENVDB_AVAILABLE
	FArrayUint8StreamBuf StreamBuf(SourceFile);
	std::istream IStream(&StreamBuf);
	openvdb::io::Stream Stream(IStream, false /*delayLoad*/);

	openvdb::GridPtrVecPtr Grids = Stream.getGrids();

	if (!Grids)
	{
		UE_LOG(LogSparseVolumeTextureOpenVDBUtility, Warning, TEXT("OpenVDB file contains no grids: %s"), *Filename);
		return false;
	}

	OutGridInfo->Empty(Grids->size());

	uint32 GridIndex = 0;
	for (openvdb::GridBase::Ptr& Grid : *Grids)
	{
		FOpenVDBGridInfo GridInfo;
		GridInfo.Index = GridIndex;
		++GridIndex;

		// Figure out the type/format of the grid
		bool bGridTypeSupported = false;
		if (Grid->isType<openvdb::FloatGrid>())
		{
			GridInfo.Format = EOpenVDBGridFormat::Float;
			bGridTypeSupported = true;
		}
		if (!bGridTypeSupported)
		{
			continue;
		}

		GridInfo.Name = Grid->getName().c_str();

		FStringFormatOrderedArguments FormatArgs;
		FormatArgs.Add(GridInfo.Name);
		FormatArgs.Add(OpenVDBGridFormatToString(GridInfo.Format));

		GridInfo.DisplayString = FString::Format(TEXT("Name: {0}, Format: {1}"), FormatArgs);

		OutGridInfo->Add(MoveTemp(GridInfo));
	}

	if (OutGridInfo->IsEmpty())
	{
		UE_LOG(LogSparseVolumeTextureOpenVDBUtility, Warning, TEXT("OpenVDB file contains no grids of supported type/format: %s"), *Filename);
		return false;
	}

	return true;

#endif // OPENVDB_AVAILABLE
	return false;
}

bool ConvertOpenVDBToSparseVolumeTexture(
	TArray<uint8>& SourceFile,
	uint32 GridIndex,
	FSparseVolumeAssetHeader* OutHeader,
	TArray<uint32>* OutDensityPage,
	TArray<uint8>* OutDensityData,
	bool bOverrideActiveMinMax,
	FVector ActiveMin,
	FVector ActiveMax)
{
#if OPENVDB_AVAILABLE
	FArrayUint8StreamBuf StreamBuf(SourceFile);
	std::istream IStream(&StreamBuf);
	openvdb::io::Stream Stream(IStream, false /*delayLoad*/);

	openvdb::GridPtrVecPtr Grids = Stream.getGrids();
	if (!Grids || Grids->size() <= GridIndex)
	{
		return false;
	}

	openvdb::FloatGrid::Ptr DensityGrid = openvdb::gridPtrCast<openvdb::FloatGrid>((*Grids)[GridIndex]);
	if (!DensityGrid)
	{
		return false;
	}

	FOpenVDBData OVDBData = GetOpenVDBData(DensityGrid);
	if (!IsOpenVDBDataValid(OVDBData, TEXT("")))
	{
		return false;
	}

	if (bOverrideActiveMinMax)
	{
		OVDBData.VolumeActiveAABBMin = ActiveMin;
		OVDBData.VolumeActiveAABBMax = ActiveMax;
		OVDBData.VolumeActiveDim = ActiveMax - ActiveMin;
	}

	FSparseVolumeAssetHeader& Header = *OutHeader;

	Header.SourceVolumeResolution = FIntVector(OVDBData.VolumeActiveDim);
	// SVT_TODO handle error too high volume resolution assuming dx11 limitations?

	Header.PageTableVolumeResolution = FIntVector3(
		FMath::DivideAndRoundUp(Header.SourceVolumeResolution.X, SPARSE_VOLUME_TILE_RES),
		FMath::DivideAndRoundUp(Header.SourceVolumeResolution.Y, SPARSE_VOLUME_TILE_RES),
		FMath::DivideAndRoundUp(Header.SourceVolumeResolution.Z, SPARSE_VOLUME_TILE_RES));
	Header.TileDataVolumeResolution = FIntVector::ZeroValue;	// unknown for now

	const uint32 VolumeTileDataBytes = SPARSE_VOLUME_TILE_RES * SPARSE_VOLUME_TILE_RES * SPARSE_VOLUME_TILE_RES;
	uint32 NumAllocatedPages = 0;

	// Allocate some memory for temp data (worst case)
	TArray<uint8> LinearAllocatedTiles;
	TArray<FIntVector3> LinearAllocatedPages;
	LinearAllocatedTiles.SetNum(Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.Z * VolumeTileDataBytes);
	LinearAllocatedPages.SetNum(Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.Z);

	auto accessor = DensityGrid->getConstAccessor();
	const float GridBackgroundValue = DensityGrid->background();

	// Go over each potential page table form the source data and push allocate it if it has any data.
	// Otherwise point to the default empty page.
	bool bAnyEmptyPageExists = false;
	for (int32_t PageZ = 0; PageZ < Header.PageTableVolumeResolution.Z; ++PageZ)
	{
		for (int32_t PageY = 0; PageY < Header.PageTableVolumeResolution.Y; ++PageY)
		{
			for (int32_t PageX = 0; PageX < Header.PageTableVolumeResolution.X; ++PageX)
			{
				bool bHasAnyData = false;

				uint8* NewTileData = LinearAllocatedTiles.GetData() + NumAllocatedPages * VolumeTileDataBytes;

				for (int32_t z = 0; z < SPARSE_VOLUME_TILE_RES; ++z)
				{
					for (int32_t y = 0; y < SPARSE_VOLUME_TILE_RES; ++y)
					{
						for (int32_t x = 0; x < SPARSE_VOLUME_TILE_RES; ++x)
						{
							FVector VoxelCoord(PageX * SPARSE_VOLUME_TILE_RES + x, PageY * SPARSE_VOLUME_TILE_RES + y, PageZ * SPARSE_VOLUME_TILE_RES + z);	// This assumes sampling outside the boundary retuirn a default value
							FVector VoxelCoord2 = OVDBData.VolumeActiveAABBMin + VoxelCoord;
							float VoxelValue = accessor.getValue(openvdb::Coord(VoxelCoord2.X, VoxelCoord2.Y, VoxelCoord2.Z));

							// clamp for now until we have fp16
							VoxelValue = FMath::Clamp(VoxelValue, 0.0f, 1.0f);

							bHasAnyData |= VoxelValue > GridBackgroundValue;

							NewTileData[z * SPARSE_VOLUME_TILE_RES * SPARSE_VOLUME_TILE_RES + y * SPARSE_VOLUME_TILE_RES + x] = uint8(VoxelValue * 255.0f);
						}
					}
				}

				if (bHasAnyData)
				{
					LinearAllocatedPages[NumAllocatedPages] = FIntVector3(PageX, PageY, PageZ);
					NumAllocatedPages++;
				}
				bAnyEmptyPageExists |= !bHasAnyData;
			}
		}
	}

	// Compute Page and Tile VolumeResolution from allocated pages
	const uint32 EffectivelyAllocatedPageEntries = NumAllocatedPages + (bAnyEmptyPageExists ? 1 : 0);
	uint32 TileVolumeResolutionCube = 1;
	while (TileVolumeResolutionCube * TileVolumeResolutionCube * TileVolumeResolutionCube < EffectivelyAllocatedPageEntries)
	{
		TileVolumeResolutionCube++;				// We use a simple loop to compute the minimum resolution of a cube to store all the tile data
	}
	Header.TileDataVolumeResolution = FIntVector3(TileVolumeResolutionCube, TileVolumeResolutionCube, TileVolumeResolutionCube);
	while (Header.TileDataVolumeResolution.X * Header.TileDataVolumeResolution.Y * (Header.TileDataVolumeResolution.Z - 1) > int32(EffectivelyAllocatedPageEntries))
	{
		Header.TileDataVolumeResolution.Z--;	// We then trim an edge to get back space.
	}
	const FIntVector3 TileCoordResolution = Header.TileDataVolumeResolution;
	Header.TileDataVolumeResolution = Header.TileDataVolumeResolution * SPARSE_VOLUME_TILE_RES;

	// Initialise the SparseVolumeTexture page and tile.
	OutDensityPage->SetNumZeroed(Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.Z);
	OutDensityData->SetNumZeroed(Header.TileDataVolumeResolution.X * Header.TileDataVolumeResolution.Y * Header.TileDataVolumeResolution.Z);
	uint32* DensityPagePtr = OutDensityPage->GetData();
	uint8* DensityDataPtr = OutDensityData->GetData();

	FIntVector DestinationTileCoord = FIntVector::ZeroValue;
	auto GoToNextTileCoord = [&]()
	{
		DestinationTileCoord.X++;
		if (DestinationTileCoord.X >= TileCoordResolution.X)
		{
			DestinationTileCoord.X = 0;
			DestinationTileCoord.Y++;
		}
		if (DestinationTileCoord.Y >= TileCoordResolution.Y)
		{
			DestinationTileCoord.Y = 0;
			DestinationTileCoord.Z++;
		}
	};

	// Add an empty tile is needed, reserve slot at coord 0
	if (bAnyEmptyPageExists)
	{
		// DensityPage is all cleared to zero, simply skip a tile
		GoToNextTileCoord();
	}

	// Generate page table and tile volume data by splatting the data
	for (uint32 i = 0; i < NumAllocatedPages; ++i)
	{
		FIntVector3 PageCoordToSplat = LinearAllocatedPages[i];
		uint8* TileDataToSplat = LinearAllocatedTiles.GetData() + i * VolumeTileDataBytes;

		// A page encodes the physical tile coord as unsigned int of 11 11 10 bits
		// This means a page coord cannot be larger than 2047 for x and y and 1023 for z
		// which mean we cannot have more than 2048*2048*1024 = 4 Giga tiles of 16^3 tiles.
		uint32 DestinationTileCoord32bit = (DestinationTileCoord.X & 0x7FF) | ((DestinationTileCoord.Y & 0x7FF) << 11) | ((DestinationTileCoord.Z & 0x3FF) << 22);

		// Setup the page table entry
		DensityPagePtr
			[
				PageCoordToSplat.Z * Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y +
				PageCoordToSplat.Y * Header.PageTableVolumeResolution.X +
				PageCoordToSplat.X
			] = DestinationTileCoord32bit;

		// Now copy the tile data
		for (int32_t z = 0; z < SPARSE_VOLUME_TILE_RES; ++z)
		{
			for (int32_t y = 0; y < SPARSE_VOLUME_TILE_RES; ++y)
			{
				for (int32_t x = 0; x < SPARSE_VOLUME_TILE_RES; ++x)
				{
					DensityDataPtr
						[
							(DestinationTileCoord.Z * SPARSE_VOLUME_TILE_RES + z) * Header.TileDataVolumeResolution.X * Header.TileDataVolumeResolution.Y +
							(DestinationTileCoord.Y * SPARSE_VOLUME_TILE_RES + y) * Header.TileDataVolumeResolution.X +
							(DestinationTileCoord.X * SPARSE_VOLUME_TILE_RES + x)
						] = TileDataToSplat[z * SPARSE_VOLUME_TILE_RES * SPARSE_VOLUME_TILE_RES + y * SPARSE_VOLUME_TILE_RES + x];
				}
			}
		}

		// Set the next tile to be written to
		GoToNextTileCoord();
	}

	return true;
#else
	return false;
#endif // OPENVDB_AVAILABLE
}

const TCHAR* OpenVDBGridFormatToString(EOpenVDBGridFormat Format)
{
	switch (Format)
	{
	case EOpenVDBGridFormat::Float:
		return TEXT("Float");
	default:
		return TEXT("Unknown");
	}
}

#endif // WITH_EDITOR