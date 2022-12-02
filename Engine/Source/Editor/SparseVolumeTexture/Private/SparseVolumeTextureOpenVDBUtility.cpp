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

	OutGridInfo->Empty(Grids->size() * 2 /*assume an average of 2 channels per grid*/);

	uint32 GridIndex = 0;
	for (openvdb::GridBase::Ptr& Grid : *Grids)
	{
		FOpenVDBGridInfo GridInfo;
		GridInfo.Index = GridIndex++;

		// Figure out the type/format of the grid
		uint32 NumComponents = 0;
		if (Grid->isType<openvdb::FloatGrid>())
		{
			NumComponents = 1;
			GridInfo.Format = EOpenVDBGridFormat::Float;
		}
#if 0 // SV_TODO: actually support these additional types in ConvertOpenVDBToSparseVolumeTexture()
		else if (Grid->isType<openvdb::Grid<openvdb::tree::Tree4<openvdb::Vec2f, 5, 4, 3>::Type>>())
		{
			NumComponents = 2;
			GridInfo.Format = EOpenVDBGridFormat::Float;
		}
		else if (Grid->isType<openvdb::Vec3SGrid>())
		{
			NumComponents = 3;
			GridInfo.Format = EOpenVDBGridFormat::Float;
		}
		else if (Grid->isType<openvdb::Grid<openvdb::tree::Tree4<openvdb::Vec4f, 5, 4, 3>::Type>>())
		{
			NumComponents = 4;
			GridInfo.Format = EOpenVDBGridFormat::Float;
		}
		else if (Grid->isType<openvdb::DoubleGrid>())
		{
			NumComponents = 1;
			GridInfo.Format = EOpenVDBGridFormat::Double;
		}
		else if (Grid->isType<openvdb::Grid<openvdb::tree::Tree4<openvdb::Vec2d, 5, 4, 3>::Type>>())
		{
			NumComponents = 2;
			GridInfo.Format = EOpenVDBGridFormat::Double;
		}
		else if (Grid->isType<openvdb::Vec3DGrid>())
		{
			NumComponents = 3;
			GridInfo.Format = EOpenVDBGridFormat::Double;
		}
		else if (Grid->isType<openvdb::Grid<openvdb::tree::Tree4<openvdb::Vec4d, 5, 4, 3>::Type>>())
		{
			NumComponents = 4;
			GridInfo.Format = EOpenVDBGridFormat::Double;
		}
#endif
		else
		{
			// unsupported type
			continue;
		}
		check(NumComponents > 0);

		GridInfo.Name = Grid->getName().c_str();

		// Create one entry per component
		for (uint32 ComponentIdx = 0; ComponentIdx < NumComponents; ++ComponentIdx)
		{
			GridInfo.ComponentIndex = ComponentIdx;

			const TCHAR* ComponentNames[] = { TEXT(".x"), TEXT(".y"),TEXT(".z"),TEXT(".w") };
			FStringFormatOrderedArguments FormatArgs;
			FormatArgs.Add(GridInfo.Name);
			FormatArgs.Add(NumComponents == 1 ? TEXT("") : ComponentNames[GridInfo.ComponentIndex]);
			FormatArgs.Add(OpenVDBGridFormatToString(GridInfo.Format));

			GridInfo.DisplayString = FString::Format(TEXT("{0}{1} ({2})"), FormatArgs);

			OutGridInfo->Add(MoveTemp(GridInfo));
		}
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

static EPixelFormat GetMultiComponentFormat(ESparseVolumePackedDataFormat Format, uint32 NumComponents)
{
	switch (Format)
	{
	case ESparseVolumePackedDataFormat::Unorm8:
	{
		switch (NumComponents)
		{
		case 1: return PF_R8;
		case 2: return PF_R8G8;
		case 3:
		case 4: return PF_R8G8B8A8;
		}
		break;
	}
	case ESparseVolumePackedDataFormat::Float16:
	{
		switch (NumComponents)
		{
		case 1: return PF_R16F;
		case 2: return PF_G16R16F;
		case 3:
		case 4: return PF_FloatRGBA;
		}
		break;
	}
	case ESparseVolumePackedDataFormat::Float32:
	{
		switch (NumComponents)
		{
		case 1: return PF_R32_FLOAT;
		case 2: return PF_G32R32F;
		case 3:
		case 4: return PF_A32B32G32R32F;
		}
		break;
	}
	}
	return PF_Unknown;
}

bool ConvertOpenVDBToSparseVolumeTexture(
	TArray<uint8>& SourceFile,
	struct FSparseVolumeRawSourcePackedData& PackedData,
	FSparseVolumeAssetHeader* OutHeader,
	TArray<uint32>* OutDensityPage,
	TArray<uint8>* OutDensityData,
	bool bOverrideActiveMinMax,
	FVector ActiveMin,
	FVector ActiveMax)
{
#if OPENVDB_AVAILABLE

	uint32 NumRequiredComponents = 0;
	for (uint32 ComponentIdx = 0; ComponentIdx < 4; ++ComponentIdx)
	{
		if (PackedData.SourceGridIndex[ComponentIdx] != INDEX_NONE)
		{
			NumRequiredComponents = FMath::Max(ComponentIdx + 1, NumRequiredComponents);
		}
	}
	const uint32 NumActualComponents = NumRequiredComponents == 3 ? 4 : NumRequiredComponents; // We don't support formats with only 3 components

	const bool bNormalizedFormat = PackedData.Format == ESparseVolumePackedDataFormat::Unorm8;
	const EPixelFormat MultiCompFormat = GetMultiComponentFormat(PackedData.Format, NumActualComponents);
	
	if (MultiCompFormat == PF_Unknown)
	{
		// SVT_TODO error message about unsupported format
		return false;
	}

	// Load file
	FArrayUint8StreamBuf StreamBuf(SourceFile);
	std::istream IStream(&StreamBuf);
	openvdb::io::Stream Stream(IStream, false /*delayLoad*/);

	// Check that at least one component has a valid grid assigned to it
	openvdb::GridPtrVecPtr Grids = Stream.getGrids();
	const size_t NumSourceGrids = Grids ? Grids->size() : 0;
	if (NumSourceGrids <= PackedData.SourceGridIndex.X
		&& NumSourceGrids <= PackedData.SourceGridIndex.Y
		&& NumSourceGrids <= PackedData.SourceGridIndex.Z
		&& NumSourceGrids <= PackedData.SourceGridIndex.W)
	{
		return false;
	}

	FSparseVolumeAssetHeader& Header = *OutHeader;
	Header.PackedDataAFormat = MultiCompFormat;
	Header.SourceVolumeResolution = FIntVector::ZeroValue;
	
	FIntVector SmallestAABBMin = FIntVector(INT32_MAX);

	// Compute per source grid data of up to 4 different grids (one per component)
	openvdb::FloatGrid::Ptr FloatGrids[4]{};
	openvdb::FloatGrid::Ptr DummyGrid = nullptr;
	float GridBackgroundValues[4]{};
	float NormalizeFactors[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
	for (uint32 CompIdx = 0; CompIdx < 4; ++CompIdx)
	{
		if (PackedData.SourceGridIndex[CompIdx] == INDEX_NONE)
		{
			FloatGrids[CompIdx] = nullptr;
			continue;
		}

		openvdb::FloatGrid::Ptr FloatGrid = openvdb::gridPtrCast<openvdb::FloatGrid>((*Grids)[PackedData.SourceGridIndex[CompIdx]]);
		if (!FloatGrid)
		{
			return false;
		}

		FOpenVDBData OVDBData = GetOpenVDBData(FloatGrid);
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

		Header.SourceVolumeResolution.X = FMath::Max(Header.SourceVolumeResolution.X, OVDBData.VolumeActiveDim.X);
		Header.SourceVolumeResolution.Y = FMath::Max(Header.SourceVolumeResolution.Y, OVDBData.VolumeActiveDim.Y);
		Header.SourceVolumeResolution.Z = FMath::Max(Header.SourceVolumeResolution.Z, OVDBData.VolumeActiveDim.Z);
		SmallestAABBMin.X = FMath::Min(SmallestAABBMin.X, OVDBData.VolumeActiveAABBMin.X);
		SmallestAABBMin.Y = FMath::Min(SmallestAABBMin.Y, OVDBData.VolumeActiveAABBMin.Y);
		SmallestAABBMin.Z = FMath::Min(SmallestAABBMin.Z, OVDBData.VolumeActiveAABBMin.Z);

		FloatGrids[CompIdx] = FloatGrid;
		DummyGrid = FloatGrid;
		GridBackgroundValues[CompIdx] = FloatGrid->background();
		if (bNormalizedFormat && PackedData.bRescaleInputForUnorm)
		{
			float MinVal = 0.0f;
			float MaxVal = 0.0f;
			FloatGrid->evalMinMax(MinVal, MaxVal);
			NormalizeFactors[CompIdx] = MaxVal > SMALL_NUMBER ? (1.0f / MaxVal) : 1.0f;
		}
	}
	
	// SVT_TODO handle error too high volume resolution assuming dx11 limitations?
	Header.PageTableVolumeResolution = FIntVector3(
		FMath::DivideAndRoundUp(Header.SourceVolumeResolution.X, SPARSE_VOLUME_TILE_RES),
		FMath::DivideAndRoundUp(Header.SourceVolumeResolution.Y, SPARSE_VOLUME_TILE_RES),
		FMath::DivideAndRoundUp(Header.SourceVolumeResolution.Z, SPARSE_VOLUME_TILE_RES));
	Header.TileDataVolumeResolution = FIntVector::ZeroValue;	// unknown for now

	const uint32 VolumeTileDataBytes = GPixelFormats[(SIZE_T)MultiCompFormat].Get3DImageSizeInBytes(SPARSE_VOLUME_TILE_RES, SPARSE_VOLUME_TILE_RES, SPARSE_VOLUME_TILE_RES);
	const uint32 FormatSize = (uint32)GPixelFormats[(SIZE_T)MultiCompFormat].BlockBytes;
	const uint32 SingleComponentFormatSize = (uint32)GPixelFormats[(SIZE_T)PackedData.Format].BlockBytes;
	uint32 NumAllocatedPages = 0;

	// Allocate some memory for temp data (worst case)
	TArray<FIntVector3> LinearAllocatedPages;
	LinearAllocatedPages.SetNum(Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.Z);

	openvdb::FloatGrid::ConstAccessor Accessors[4]
	{
		(FloatGrids[0] ? FloatGrids[0] : DummyGrid)->getConstAccessor(),
		(FloatGrids[1] ? FloatGrids[1] : DummyGrid)->getConstAccessor(),
		(FloatGrids[2] ? FloatGrids[2] : DummyGrid)->getConstAccessor(),
		(FloatGrids[3] ? FloatGrids[3] : DummyGrid)->getConstAccessor(),
	};

	// Go over each potential page from the source data and push allocate it if it has any data.
	// Otherwise point to the default empty page.
	bool bAnyEmptyPageExists = false;
	for (int32_t PageZ = 0; PageZ < Header.PageTableVolumeResolution.Z; ++PageZ)
	{
		for (int32_t PageY = 0; PageY < Header.PageTableVolumeResolution.Y; ++PageY)
		{
			for (int32_t PageX = 0; PageX < Header.PageTableVolumeResolution.X; ++PageX)
			{
				bool bHasAnyData = false;

				for (int32_t z = 0; z < SPARSE_VOLUME_TILE_RES; ++z)
				{
					for (int32_t y = 0; y < SPARSE_VOLUME_TILE_RES; ++y)
					{
						for (int32_t x = 0; x < SPARSE_VOLUME_TILE_RES; ++x)
						{
							for (uint32 CompIdx = 0; CompIdx < NumActualComponents; ++CompIdx)
							{
								if (PackedData.SourceGridIndex[CompIdx] != INDEX_NONE)
								{
									FVector VoxelCoord = FVector(SmallestAABBMin) + FVector(PageX, PageY, PageZ) * SPARSE_VOLUME_TILE_RES + FVector(x, y, z);	// This assumes sampling outside the boundary returns a default value
									float VoxelValue = Accessors[CompIdx].getValue(openvdb::Coord(VoxelCoord.X, VoxelCoord.Y, VoxelCoord.Z));
									bHasAnyData |= VoxelValue > GridBackgroundValues[CompIdx];
								}
							}
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
	OutDensityData->SetNumZeroed(Header.TileDataVolumeResolution.X * Header.TileDataVolumeResolution.Y * Header.TileDataVolumeResolution.Z * FormatSize);
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

		// Now copy the tile data from the source to the page atlas
		for (int32_t z = 0; z < SPARSE_VOLUME_TILE_RES; ++z)
		{
			for (int32_t y = 0; y < SPARSE_VOLUME_TILE_RES; ++y)
			{
				for (int32_t x = 0; x < SPARSE_VOLUME_TILE_RES; ++x)
				{
					for (uint32 CompIdx = 0; CompIdx < NumActualComponents; ++CompIdx)
					{
						float VoxelValue = 0.0f;
						if (PackedData.SourceGridIndex[CompIdx] != INDEX_NONE)
						{
							FVector VoxelCoord = FVector(SmallestAABBMin) + FVector(PageCoordToSplat * SPARSE_VOLUME_TILE_RES) + FVector(x, y, z);	// This assumes sampling outside the boundary returns a default value
							VoxelValue = Accessors[CompIdx].getValue(openvdb::Coord(VoxelCoord.X, VoxelCoord.Y, VoxelCoord.Z));
						}

						const SIZE_T DstCoord = ((DestinationTileCoord.Z * SPARSE_VOLUME_TILE_RES + z) * Header.TileDataVolumeResolution.X * Header.TileDataVolumeResolution.Y +
							(DestinationTileCoord.Y * SPARSE_VOLUME_TILE_RES + y) * Header.TileDataVolumeResolution.X +
							(DestinationTileCoord.X * SPARSE_VOLUME_TILE_RES + x)) * FormatSize + CompIdx * SingleComponentFormatSize;

						switch (PackedData.Format)
						{
						case ESparseVolumePackedDataFormat::Unorm8:
						{
							DensityDataPtr[DstCoord] = uint8(FMath::Clamp(VoxelValue * NormalizeFactors[CompIdx], 0.0f, 1.0f) * 255.0f);
							break;
						}
						case ESparseVolumePackedDataFormat::Float16:
						{
							const uint16 VoxelValue16FEncoded = FFloat16(VoxelValue).Encoded;
							*((uint16*)(&DensityDataPtr[DstCoord])) = VoxelValue16FEncoded;
							break;
						}
						case ESparseVolumePackedDataFormat::Float32:
						{
							*((float*)(&DensityDataPtr[DstCoord])) = VoxelValue;
							break;
						}
						default:
							checkNoEntry();
							break;
						}
					}
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
	case EOpenVDBGridFormat::Double:
		return TEXT("Double");
	default:
		return TEXT("Unknown");
	}
}

#endif // WITH_EDITOR