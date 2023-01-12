// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "SparseVolumeTextureOpenVDBUtility.h"
#include "SparseVolumeTextureOpenVDB.h"
#include "SparseVolumeTexture/SparseVolumeTexture.h"
#include "OpenVDBGridAdapter.h"

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

bool IsOpenVDBDataValid(const FOpenVDBData& OpenVDBData, const FString& Filename)
{
	if (OpenVDBData.VolumeActiveDim.X * OpenVDBData.VolumeActiveDim.Y * OpenVDBData.VolumeActiveDim.Z == 0)
	{
		// SVT_TODO we should gently handle that case
		UE_LOG(LogSparseVolumeTextureOpenVDBUtility, Warning, TEXT("OpenVDB grid is empty due to volume size being 0: %s"), *Filename);
		return false;
	}

	if (!OpenVDBData.bHasUniformVoxels)
	{
		UE_LOG(LogSparseVolumeTextureOpenVDBUtility, Warning, TEXT("OpenVDB importer cannot handle non uniform voxels: %s"), *Filename);
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

bool GetOpenVDBGridInfo(TArray<uint8>& SourceFile, TArray<FOpenVDBGridInfo>* OutGridInfo)
{
#if OPENVDB_AVAILABLE
	FArrayUint8StreamBuf StreamBuf(SourceFile);
	std::istream IStream(&StreamBuf);
	openvdb::io::Stream Stream(IStream, false /*delayLoad*/);

	openvdb::GridPtrVecPtr Grids = Stream.getGrids();

	OutGridInfo->Empty(Grids->size());

	uint32 GridIndex = 0;
	for (openvdb::GridBase::Ptr& Grid : *Grids)
	{
		// Create entry
		FOpenVDBGridInfo GridInfo;
		GridInfo.Index = GridIndex++;
		GridInfo.NumComponents = 0;
		GridInfo.Type = EOpenVDBGridType::Unknown;
		GridInfo.Name = Grid->getName().c_str();
		GridInfo.OpenVDBData = GetOpenVDBData(Grid);

		// Figure out the type/format of the grid
		if (Grid->isType<FOpenVDBFloat1Grid>())
		{
			GridInfo.NumComponents = 1;
			GridInfo.Type = EOpenVDBGridType::Float;
		}
		else if (Grid->isType<FOpenVDBFloat2Grid>())
		{
			GridInfo.NumComponents = 2;
			GridInfo.Type = EOpenVDBGridType::Float2;
		}
		else if (Grid->isType<FOpenVDBFloat3Grid>())
		{
			GridInfo.NumComponents = 3;
			GridInfo.Type = EOpenVDBGridType::Float3;
		}
		else if (Grid->isType<FOpenVDBFloat4Grid>())
		{
			GridInfo.NumComponents = 4;
			GridInfo.Type = EOpenVDBGridType::Float4;
		}
		else if (Grid->isType<FOpenVDBDouble1Grid>())
		{
			GridInfo.NumComponents = 1;
			GridInfo.Type = EOpenVDBGridType::Double;
		}
		else if (Grid->isType<FOpenVDBDouble2Grid>())
		{
			GridInfo.NumComponents = 2;
			GridInfo.Type = EOpenVDBGridType::Double2;
		}
		else if (Grid->isType<FOpenVDBDouble3Grid>())
		{
			GridInfo.NumComponents = 3;
			GridInfo.Type = EOpenVDBGridType::Double3;
		}
		else if (Grid->isType<FOpenVDBDouble4Grid>())
		{
			GridInfo.NumComponents = 4;
			GridInfo.Type = EOpenVDBGridType::Double4;
		}

		FStringFormatOrderedArguments FormatArgs;
		FormatArgs.Add(GridInfo.Index);
		FormatArgs.Add(OpenVDBGridTypeToString(GridInfo.Type));
		FormatArgs.Add(GridInfo.Name);

		GridInfo.DisplayString = FString::Format(TEXT("{0}. Type: {1}, Name: \"{2}\""), FormatArgs);

		OutGridInfo->Add(MoveTemp(GridInfo));
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
	FSparseVolumeRawSourcePackedData& PackedDataA,
	FSparseVolumeRawSourcePackedData& PackedDataB,
	FOpenVDBToSVTConversionResult* OutResult,
	bool bOverrideActiveMinMax,
	FVector ActiveMin,
	FVector ActiveMax)
{
#if OPENVDB_AVAILABLE

	constexpr uint32 NumPackedData = 2; // PackedDataA and PackedDataB, representing the two textures with voxel data
	FSparseVolumeRawSourcePackedData* PackedData[NumPackedData] = { &PackedDataA, &PackedDataB };

	// Compute some basic info about the number of components and which format to use
	uint32 NumActualComponents[NumPackedData] = {};
	EPixelFormat MultiCompFormat[NumPackedData] = {};
	uint32 FormatSize[NumPackedData] = {};
	uint32 SingleComponentFormatSize[NumPackedData] = {};
	bool bNormalizedFormat[NumPackedData] = {};
	bool bHasValidSourceGrids[NumPackedData] = {};
	bool bAnySourceGridIndicesValid = false;
	
	for (uint32 PackedDataIdx = 0; PackedDataIdx < NumPackedData; ++PackedDataIdx)
	{
		uint32 NumRequiredComponents = 0;
		for (uint32 ComponentIdx = 0; ComponentIdx < 4; ++ComponentIdx)
		{
			if (PackedData[PackedDataIdx]->SourceGridIndex[ComponentIdx] != INDEX_NONE)
			{
				check(PackedData[PackedDataIdx]->SourceComponentIndex[ComponentIdx] != INDEX_NONE);
				NumRequiredComponents = FMath::Max(ComponentIdx + 1, NumRequiredComponents);
				bHasValidSourceGrids[PackedDataIdx] = true;
				bAnySourceGridIndicesValid = true;
			}
		}

		if (bHasValidSourceGrids[PackedDataIdx])
		{
			NumActualComponents[PackedDataIdx] = NumRequiredComponents == 3 ? 4 : NumRequiredComponents; // We don't support formats with only 3 components
			bNormalizedFormat[PackedDataIdx] = PackedData[PackedDataIdx]->Format == ESparseVolumePackedDataFormat::Unorm8;
			MultiCompFormat[PackedDataIdx] = GetMultiComponentFormat(PackedData[PackedDataIdx]->Format, NumActualComponents[PackedDataIdx]);

			if (MultiCompFormat[PackedDataIdx] == PF_Unknown)
			{
				// SVT_TODO error message about unsupported format
				return false;
			}

			FormatSize[PackedDataIdx] = (uint32)GPixelFormats[(SIZE_T)MultiCompFormat[PackedDataIdx]].BlockBytes;
			SingleComponentFormatSize[PackedDataIdx] = FormatSize[PackedDataIdx] / NumActualComponents[PackedDataIdx];
		}
	}

	// All source grid indices are INDEX_NONE, so nothing was selected for import
	if (!bAnySourceGridIndicesValid)
	{
		// SVT_TODO error message
		return false;
	}

	// Load file
	FArrayUint8StreamBuf StreamBuf(SourceFile);
	std::istream IStream(&StreamBuf);
	openvdb::io::Stream Stream(IStream, false /*delayLoad*/);

	// Check that the source grid indices are valid
	openvdb::GridPtrVecPtr Grids = Stream.getGrids();
	const size_t NumSourceGrids = Grids ? Grids->size() : 0;
	for (uint32 PackedDataIdx = 0; PackedDataIdx < NumPackedData; ++PackedDataIdx)
	{
		for (uint32 CompIdx = 0; CompIdx < 4; ++CompIdx)
		{
			const uint32 SourceGridIndex = PackedData[PackedDataIdx]->SourceGridIndex[CompIdx];
			if (SourceGridIndex != INDEX_NONE && SourceGridIndex >= NumSourceGrids)
			{
				// SVT_TODO error message
				return false;
			}
		}
	}

	FSparseVolumeAssetHeader& Header = *OutResult->Header;
	Header.PackedDataAFormat = MultiCompFormat[0];
	Header.PackedDataBFormat = MultiCompFormat[1];
	Header.SourceVolumeResolution = FIntVector::ZeroValue;
	
	FIntVector SmallestAABBMin = FIntVector(INT32_MAX);

	// Compute per source grid data of up to 4 different grids (one per component)
	TArray<TSharedPtr<IOpenVDBGridAdapterBase>> UniqueGridAdapters;
	UniqueGridAdapters.SetNum((int32)Grids->size());
	TSharedPtr<IOpenVDBGridAdapterBase> GridAdapters[NumPackedData][4]{};
	float GridBackgroundValues[NumPackedData][4]{};
	float NormalizeScale[NumPackedData][4]{};
	float NormalizeBias[NumPackedData][4]{};
	for (uint32 PackedDataIdx = 0; PackedDataIdx < NumPackedData; ++PackedDataIdx)
	{
		for (uint32 CompIdx = 0; CompIdx < 4; ++CompIdx)
		{
			NormalizeScale[PackedDataIdx][CompIdx] = 1.0f;
			const uint32 SourceGridIndex = PackedData[PackedDataIdx]->SourceGridIndex[CompIdx];
			const uint32 SourceComponentIndex = PackedData[PackedDataIdx]->SourceComponentIndex[CompIdx];
			if (SourceGridIndex == INDEX_NONE)
			{
				continue;
			}

			openvdb::GridBase::Ptr GridBase = (*Grids)[SourceGridIndex];

			// Try to reuse adapters. Internally they use caching to accelerate read accesses, 
			// so using three different adapters to access the three components of a single grid would be wasteful.
			if (UniqueGridAdapters[SourceGridIndex] == nullptr)
			{
				UniqueGridAdapters[SourceGridIndex] = CreateOpenVDBGridAdapter(GridBase);
				if (!UniqueGridAdapters[SourceGridIndex])
				{
					return false;
				}
			}

			GridAdapters[PackedDataIdx][CompIdx] = UniqueGridAdapters[SourceGridIndex];

			FOpenVDBData OVDBData = GetOpenVDBData(GridBase);
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

			GridBackgroundValues[PackedDataIdx][CompIdx] = GridAdapters[PackedDataIdx][CompIdx]->GetBackgroundValue(SourceComponentIndex);
			if (bNormalizedFormat[PackedDataIdx] && PackedData[PackedDataIdx]->bRemapInputForUnorm)
			{
				float MinVal = 0.0f;
				float MaxVal = 0.0f;
				GridAdapters[PackedDataIdx][CompIdx]->GetMinMaxValue(SourceComponentIndex, &MinVal, &MaxVal);
				const float Diff = MaxVal - MinVal;
				NormalizeScale[PackedDataIdx][CompIdx] = MaxVal > SMALL_NUMBER ? (1.0f / Diff) : 1.0f;
				NormalizeBias[PackedDataIdx][CompIdx] = -MinVal * NormalizeScale[PackedDataIdx][CompIdx];
			}
		}
	}
	
	// SVT_TODO handle error too high volume resolution assuming dx11 limitations?
	Header.PageTableVolumeResolution = FIntVector3(
		FMath::DivideAndRoundUp(Header.SourceVolumeResolution.X, SPARSE_VOLUME_TILE_RES),
		FMath::DivideAndRoundUp(Header.SourceVolumeResolution.Y, SPARSE_VOLUME_TILE_RES),
		FMath::DivideAndRoundUp(Header.SourceVolumeResolution.Z, SPARSE_VOLUME_TILE_RES));
	Header.TileDataVolumeResolution = FIntVector::ZeroValue;	// unknown for now

	uint32 NumAllocatedPages = 0;

	// Allocate some memory for temp data (worst case)
	TArray<FIntVector3> LinearAllocatedPages;
	LinearAllocatedPages.SetNum(Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.Z);

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

				for (uint32 PackedDataIdx = 0; PackedDataIdx < NumPackedData; ++PackedDataIdx)
				{
					if (!bHasValidSourceGrids[PackedDataIdx])
					{
						continue;
					}
					for (int32_t z = 0; z < SPARSE_VOLUME_TILE_RES; ++z)
					{
						for (int32_t y = 0; y < SPARSE_VOLUME_TILE_RES; ++y)
						{
							for (int32_t x = 0; x < SPARSE_VOLUME_TILE_RES; ++x)
							{
								for (uint32 CompIdx = 0; CompIdx < NumActualComponents[PackedDataIdx]; ++CompIdx)
								{
									if (PackedData[PackedDataIdx]->SourceGridIndex[CompIdx] != INDEX_NONE)
									{
										FVector VoxelCoord = FVector(SmallestAABBMin) + FVector(PageX, PageY, PageZ) * SPARSE_VOLUME_TILE_RES + FVector(x, y, z);	// This assumes sampling outside the boundary returns a default value
										float VoxelValue = GridAdapters[PackedDataIdx][CompIdx]->Sample(openvdb::Coord(VoxelCoord.X, VoxelCoord.Y, VoxelCoord.Z), PackedData[PackedDataIdx]->SourceComponentIndex[CompIdx]);
										bHasAnyData |= VoxelValue != GridBackgroundValues[PackedDataIdx][CompIdx];
									}
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
	OutResult->PageTable->SetNumZeroed(Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.Z);
	OutResult->PhysicalTileDataA->SetNumZeroed(Header.TileDataVolumeResolution.X * Header.TileDataVolumeResolution.Y * Header.TileDataVolumeResolution.Z * FormatSize[0] * (bHasValidSourceGrids[0] ? 1 : 0));
	OutResult->PhysicalTileDataB->SetNumZeroed(Header.TileDataVolumeResolution.X * Header.TileDataVolumeResolution.Y * Header.TileDataVolumeResolution.Z * FormatSize[1] * (bHasValidSourceGrids[1] ? 1 : 0));
	uint32* PageTablePtr = OutResult->PageTable->GetData();
	uint8* PhysicalTileDataPtrs[] = { OutResult->PhysicalTileDataA->GetData(), OutResult->PhysicalTileDataB->GetData() };

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
		// PageTable is all cleared to zero, simply skip a tile
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
		PageTablePtr
			[
				PageCoordToSplat.Z * Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y +
				PageCoordToSplat.Y * Header.PageTableVolumeResolution.X +
				PageCoordToSplat.X
			] = DestinationTileCoord32bit;

		// Now copy the tile data from the source to the page atlas
		for (uint32 PackedDataIdx = 0; PackedDataIdx < NumPackedData; ++PackedDataIdx)
		{
			if (!bHasValidSourceGrids[PackedDataIdx])
			{
				continue;
			}
			uint8* PhysicalTileDataPtr = PhysicalTileDataPtrs[PackedDataIdx];
			for (int32_t z = 0; z < SPARSE_VOLUME_TILE_RES; ++z)
			{
				for (int32_t y = 0; y < SPARSE_VOLUME_TILE_RES; ++y)
				{
					for (int32_t x = 0; x < SPARSE_VOLUME_TILE_RES; ++x)
					{
						for (uint32 CompIdx = 0; CompIdx < NumActualComponents[PackedDataIdx]; ++CompIdx)
						{
							float VoxelValue = 0.0f;
							float VoxelValueNormalized = 0.0f;
							if (PackedData[PackedDataIdx]->SourceGridIndex[CompIdx] != INDEX_NONE)
							{
								FVector VoxelCoord = FVector(SmallestAABBMin) + FVector(PageCoordToSplat * SPARSE_VOLUME_TILE_RES) + FVector(x, y, z);	// This assumes sampling outside the boundary returns a default value
								VoxelValue = GridAdapters[PackedDataIdx][CompIdx]->Sample(openvdb::Coord(VoxelCoord.X, VoxelCoord.Y, VoxelCoord.Z), PackedData[PackedDataIdx]->SourceComponentIndex[CompIdx]);
								VoxelValueNormalized = FMath::Clamp(VoxelValue * NormalizeScale[PackedDataIdx][CompIdx] + NormalizeBias[PackedDataIdx][CompIdx], 0.0f, 1.0f);
							}

							const SIZE_T DstCoord = ((DestinationTileCoord.Z * SPARSE_VOLUME_TILE_RES + z) * Header.TileDataVolumeResolution.X * Header.TileDataVolumeResolution.Y +
								(DestinationTileCoord.Y * SPARSE_VOLUME_TILE_RES + y) * Header.TileDataVolumeResolution.X +
								(DestinationTileCoord.X * SPARSE_VOLUME_TILE_RES + x)) * FormatSize[PackedDataIdx] + CompIdx * SingleComponentFormatSize[PackedDataIdx];

							switch (PackedData[PackedDataIdx]->Format)
							{
							case ESparseVolumePackedDataFormat::Unorm8:
							{
								PhysicalTileDataPtr[DstCoord] = uint8(VoxelValueNormalized * 255.0f);
								break;
							}
							case ESparseVolumePackedDataFormat::Float16:
							{
								const uint16 VoxelValue16FEncoded = FFloat16(VoxelValue).Encoded;
								*((uint16*)(&PhysicalTileDataPtr[DstCoord])) = VoxelValue16FEncoded;
								break;
							}
							case ESparseVolumePackedDataFormat::Float32:
							{
								*((float*)(&PhysicalTileDataPtr[DstCoord])) = VoxelValue;
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
		}

		// Set the next tile to be written to
		GoToNextTileCoord();
	}

	return true;
#else
	return false;
#endif // OPENVDB_AVAILABLE
}

const TCHAR* OpenVDBGridTypeToString(EOpenVDBGridType Type)
{
	switch (Type)
	{
	case EOpenVDBGridType::Float:
		return TEXT("Float");
	case EOpenVDBGridType::Float2:
		return TEXT("Float2");
	case EOpenVDBGridType::Float3:
		return TEXT("Float3");
	case EOpenVDBGridType::Float4:
		return TEXT("Float4");
	case EOpenVDBGridType::Double:
		return TEXT("Double");
	case EOpenVDBGridType::Double2:
		return TEXT("Double2");
	case EOpenVDBGridType::Double3:
		return TEXT("Double3");
	case EOpenVDBGridType::Double4:
		return TEXT("Double4");
	default:
		return TEXT("Unknown");
	}
}

#endif // WITH_EDITOR