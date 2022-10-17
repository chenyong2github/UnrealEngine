// Copyright Epic Games, Inc. All Rights Reserved.

#include "SparseVolumeTextureFactory.h"

#include "SparseVolumeTexture/SparseVolumeTexture.h"

#if WITH_EDITOR

#include "SparseVolumeTextureOpenVDB.h"

#include "Serialization/EditorBulkDataWriter.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"

#include "Editor.h"

#define LOCTEXT_NAMESPACE "USparseVolumeTextureFactory"

DEFINE_LOG_CATEGORY_STATIC(LogSparseVolumeTextureFactory, Log, All);

USparseVolumeTextureFactory::USparseVolumeTextureFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	bEditorImport = true;
	SupportedClass = USparseVolumeTexture::StaticClass();

	Formats.Add(TEXT("vdb;OpenVDB Format"));
}

FText USparseVolumeTextureFactory::GetDisplayName() const
{
	return LOCTEXT("SparseVolumeTextureFactoryDescription", "Sparse Volume Texture");
}

bool USparseVolumeTextureFactory::ConfigureProperties()
{
	return true;
}

bool USparseVolumeTextureFactory::ShouldShowInNewMenu() const
{
	return false;
}


///////////////////////////////////////////////////////////////////////////////
// Create asset


bool USparseVolumeTextureFactory::CanCreateNew() const
{
	return false;	// To be able to import files and call 
}

UObject* USparseVolumeTextureFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	USparseVolumeTexture* Object = NewObject<USparseVolumeTexture>(InParent, InClass, InName, Flags);

	// SVT_TODO initialize similarly to UTexture2DFactoryNew

	return Object;
}


///////////////////////////////////////////////////////////////////////////////
// Import asset


bool USparseVolumeTextureFactory::DoesSupportClass(UClass* Class)
{
	return Class == USparseVolumeTexture::StaticClass();
}

UClass* USparseVolumeTextureFactory::ResolveSupportedClass()
{
	return USparseVolumeTexture::StaticClass();
}

bool USparseVolumeTextureFactory::FactoryCanImport(const FString& Filename)
{
	const FString Extension = FPaths::GetExtension(Filename);
	if (Extension == TEXT("vdb"))
	{
		return true;
	}
	return false;
}

void USparseVolumeTextureFactory::CleanUp()
{
	Super::CleanUp();
}

struct FOpenVDBData
{
	FVector VolumeActiveAABBMin;
	FVector VolumeActiveAABBMax;
	FVector VolumeActiveDim;
	FVector VolumeVoxelSize;
	bool bIsInWorldSpace;
	bool bHasUniformVoxels;
};

#if OPENVDB_AVAILABLE

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

static bool IsOpenVDBDataValid(FOpenVDBData& OpenVDBData, const FString& Filename)
{
	if (OpenVDBData.VolumeActiveDim.X * OpenVDBData.VolumeActiveDim.Y * OpenVDBData.VolumeActiveDim.Z == 0)
	{
		// SVT_TODO we should gently handle that case
		UE_LOG(LogSparseVolumeTextureFactory, Error, TEXT("Could not import empty OpenVDB asset due to volume size being 0: %s"), *Filename);
		return false;
	}

	if (!OpenVDBData.bHasUniformVoxels)
	{
		UE_LOG(LogSparseVolumeTextureFactory, Error, TEXT("OpenVDB importer cannot handle non uniform voxels: %s"), *Filename);
		return false;
	}
	return true;
}


static bool ConvertOpenVDBToSparseVolumeTexture(
	openvdb::FloatGrid::Ptr DensityGrid,
	const FString& SourceFilename,
	FSparseVolumeRawSource& OutSparseVolumeRawSource,
	bool bOverrideActiveMinMax = false,
	FVector ActiveMin = FVector::ZeroVector,
	FVector ActiveMax = FVector::ZeroVector)
{
	FOpenVDBData OVDBData = GetOpenVDBData(DensityGrid);
	if (!IsOpenVDBDataValid(OVDBData, SourceFilename))
	{
		return false;
	}

	if (bOverrideActiveMinMax)
	{
		OVDBData.VolumeActiveAABBMin = ActiveMin;
		OVDBData.VolumeActiveAABBMax = ActiveMax;
		OVDBData.VolumeActiveDim = ActiveMax - ActiveMin;
	}

	FSparseVolumeAssetHeader& Header = OutSparseVolumeRawSource.Header;

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
	// It would be good to stream in the OpenVDB file there but could not find a way to do that yet so we just store the sparse texture.
	OutSparseVolumeRawSource.DensityPage.SetNumZeroed(Header.PageTableVolumeResolution.X * Header.PageTableVolumeResolution.Y * Header.PageTableVolumeResolution.Z);
	OutSparseVolumeRawSource.DensityData.SetNumZeroed(Header.TileDataVolumeResolution.X * Header.TileDataVolumeResolution.Y * Header.TileDataVolumeResolution.Z);

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
		OutSparseVolumeRawSource.DensityPage.GetData()
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
					OutSparseVolumeRawSource.DensityData.GetData()
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
}

struct FOpenVDBFrameData
{
	FString Filename;
	openvdb::io::File* File;

	FOpenVDBData OpenVDBData;

	openvdb::FloatGrid::Ptr DensityGrid;
};

#endif // OPENVDB_AVAILABLE


UObject* USparseVolumeTextureFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename,
	const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{

#if OPENVDB_AVAILABLE

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, InClass, InParent, InName, Parms);
	TArray<UObject*> ResultAssets;

	auto IsFilePotentiallyPartOfASequence = [](const FString& Filename)
	{
		// The file is potnetially a sequence of the character before the `.vdb` is a number.
		return FChar::IsDigit(Filename[Filename.Len() - 5]);
	};

	if (IsFilePotentiallyPartOfASequence(Filename))
	{
		// Import as an animated sparse volume texture asset.

		const FString FilenameWithoutExt = Filename.LeftChop(4);
		const int32 LastNonDigitIndex = FilenameWithoutExt.FindLastCharByPredicate([](TCHAR Letter) { return !FChar::IsDigit(Letter); }) + 1;
		const int32 DigitCount = FilenameWithoutExt.Len() - LastNonDigitIndex;
		FString FilenameWithoutSuffix = FilenameWithoutExt.LeftChop(FilenameWithoutExt.Len() - LastNonDigitIndex);
		TCHAR LastDigit = FilenameWithoutExt[FilenameWithoutExt.Len() - 5];

		bool IndexStartsAtOne = false;
		auto GetOpenVDBFileNameForFrame = [&](int32 FrameIndex)
		{
			FString IndexString = FString::FromInt(FrameIndex + (IndexStartsAtOne ? 1 : 0));
			// User must select a frame with index in [0-9] so that we can count leading 0s
			check(DigitCount==1 || (DigitCount>1 && IndexString.Len() <= DigitCount));
			const int32 MissingLeadingZeroCount = DigitCount - IndexString.Len();
			const FString StringZero = FString::FromInt(0);
			for (int32 i = 0; i < MissingLeadingZeroCount; ++i)
			{
				IndexString = StringZero + IndexString;
			}
			return FString(FilenameWithoutSuffix + IndexString) + TEXT(".vdb");
		};

		const FString VDBFileAt0 = GetOpenVDBFileNameForFrame(0);
		const FString VDBFileAt1 = GetOpenVDBFileNameForFrame(1);
		const bool VDBFileAt0Exists = FPaths::FileExists(VDBFileAt0);
		const bool VDBFileAt1Exists = FPaths::FileExists(VDBFileAt1);
		if (!VDBFileAt0Exists && !VDBFileAt1Exists)
		{
			UE_LOG(LogSparseVolumeTextureFactory, Error, TEXT("An OpenVDB animated sequence must start at index 0 or 1: %s or %s not found."), *VDBFileAt0, *VDBFileAt1);
			return nullptr;
		}
		IndexStartsAtOne = !VDBFileAt0Exists;

		FName NewName(InName .ToString()+ TEXT("VDBAnim"));
		UAnimatedSparseVolumeTexture* AnimatedSVTexture = NewObject<UAnimatedSparseVolumeTexture>(InParent, UAnimatedSparseVolumeTexture::StaticClass(), NewName, Flags);

		// Go over all the frame index and stop at the first missing one.
		int32 FrameCount = 0;
		while(FPaths::FileExists(GetOpenVDBFileNameForFrame(FrameCount)))
		{
			FrameCount++;
		}
		TArray<FOpenVDBFrameData> OpenVDBFramesData;
		OpenVDBFramesData.SetNum(FrameCount);

		FScopedSlowTask ImportTask(FrameCount, LOCTEXT("ImportingVDBAnim", "Importing OpenVDB animation"));
		ImportTask.MakeDialog(true);

		// Open all each frame openvdb files.
		for(int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			FOpenVDBFrameData& OpenVDBFrameData = OpenVDBFramesData[FrameIndex];
			OpenVDBFrameData.Filename = GetOpenVDBFileNameForFrame(FrameIndex);

			std::string FileNameStr(TCHAR_TO_ANSI(*OpenVDBFrameData.Filename));
			OpenVDBFrameData.File = new openvdb::io::File(FileNameStr.c_str());
			try
			{
				OpenVDBFrameData.File->open();
			}
			catch (openvdb::Exception e)
			{
				UE_LOG(LogSparseVolumeTextureFactory, Error, TEXT("OpenVDB file coult not be opened: %s"), *OpenVDBFrameData.Filename);
				return nullptr;
			}

			openvdb::GridBase::Ptr BaseGrid = nullptr;
			for (openvdb::io::File::NameIterator nameIter = OpenVDBFrameData.File->beginName();
				nameIter != OpenVDBFrameData.File->endName(); ++nameIter)
			{
				if (nameIter.gridName() == "density")
				{
					openvdb::GridBase::Ptr QueryGrid = OpenVDBFrameData.File->readGrid(nameIter.gridName());
					if (QueryGrid->isType<openvdb::FloatGrid>())
					{
						BaseGrid = QueryGrid;
						break;
					}
					break;
				}
			}

			// If we have not found any density map, let's pick up the first float map
			for (openvdb::io::File::NameIterator nameIter = OpenVDBFrameData.File->beginName();
				nameIter != OpenVDBFrameData.File->endName(); ++nameIter)
			{
				openvdb::GridBase::Ptr QueryGrid = OpenVDBFrameData.File->readGrid(nameIter.gridName());
				if (QueryGrid->isType<openvdb::FloatGrid>())
				{
					BaseGrid = QueryGrid;
					break;
				}
			}

			OpenVDBFrameData.DensityGrid = openvdb::gridPtrCast<openvdb::FloatGrid>(BaseGrid);

			if (OpenVDBFrameData.DensityGrid == nullptr)
			{
				UE_LOG(LogSparseVolumeTextureFactory, Error, TEXT("Could not find a Grid named density or grid of type float: %s"), *OpenVDBFrameData.Filename);
				return nullptr;
			}

			OpenVDBFrameData.OpenVDBData = GetOpenVDBData(OpenVDBFrameData.DensityGrid);
			if (!IsOpenVDBDataValid(OpenVDBFrameData.OpenVDBData, OpenVDBFrameData.Filename))
			{
				return nullptr;
			}
		}


		// Evaluate the maximum extent for all frames.
		FVector AnimationAABBMin;
		FVector AnimationAABBMax;
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			FOpenVDBFrameData& OpenVDBFrameData = OpenVDBFramesData[FrameIndex];
			AnimationAABBMin = FrameIndex == 0 ? OpenVDBFrameData.OpenVDBData.VolumeActiveAABBMin : FVector::Min(AnimationAABBMin, OpenVDBFrameData.OpenVDBData.VolumeActiveAABBMin);
			AnimationAABBMax = FrameIndex == 0 ? OpenVDBFrameData.OpenVDBData.VolumeActiveAABBMax : FVector::Max(AnimationAABBMin, OpenVDBFrameData.OpenVDBData.VolumeActiveAABBMax);
		}

		// Allocate space for each frame
		AnimatedSVTexture->FrameCount = FrameCount;
		AnimatedSVTexture->AnimationFrames.SetNum(FrameCount);

		UE_LOG(LogSparseVolumeTextureFactory, Display, TEXT("Serializing: %i frame"), FrameCount);

		// Convert all openvdb files to sparse volume texture
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			FOpenVDBFrameData& OpenVDBFrameData = OpenVDBFramesData[FrameIndex];
			FSparseVolumeRawSource SparseVolumeRawSource;
			if (!ConvertOpenVDBToSparseVolumeTexture(OpenVDBFrameData.DensityGrid, OpenVDBFrameData.Filename, SparseVolumeRawSource,
				true, AnimationAABBMin, AnimationAABBMax))
			{
				return nullptr;
			}

			// Serialise the raw source data fro this frame into the asset object.
			{
				UE_LOG(LogSparseVolumeTextureFactory, Display, TEXT("  - frame %i (active dimension %i x %i x %i)"), FrameIndex, 
					OpenVDBFrameData.OpenVDBData.VolumeActiveDim.X, OpenVDBFrameData.OpenVDBData.VolumeActiveDim.Y, OpenVDBFrameData.OpenVDBData.VolumeActiveDim.Z);

				UE_LOG(LogSparseVolumeTextureFactory, Display, TEXT("    -  SourceVolumeResolution %i x %i x %i"),
					SparseVolumeRawSource.Header.SourceVolumeResolution.X, SparseVolumeRawSource.Header.SourceVolumeResolution.Y, SparseVolumeRawSource.Header.SourceVolumeResolution.Z);
				UE_LOG(LogSparseVolumeTextureFactory, Display, TEXT("    -  PageTableVolumeResolution %i x %i x %i"),
					SparseVolumeRawSource.Header.PageTableVolumeResolution.X, SparseVolumeRawSource.Header.PageTableVolumeResolution.Y, SparseVolumeRawSource.Header.PageTableVolumeResolution.Z);
				UE_LOG(LogSparseVolumeTextureFactory, Display, TEXT("    -  TileDataVolumeResolution %i x %i x %i"),
					SparseVolumeRawSource.Header.TileDataVolumeResolution.X, SparseVolumeRawSource.Header.TileDataVolumeResolution.Y, SparseVolumeRawSource.Header.TileDataVolumeResolution.Z);

				UE::Serialization::FEditorBulkDataWriter RawDataArchiveWriter(AnimatedSVTexture->AnimationFrames[FrameIndex].RawData);
				SparseVolumeRawSource.Serialize(RawDataArchiveWriter);

				if (ImportTask.ShouldCancel())
				{
					return nullptr;
				}
				ImportTask.EnterProgressFrame(1.0f, LOCTEXT("ConvertingVDBStatic", "Converting static OpenVDB"));
			}
		}

		// Close all the files.
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			FOpenVDBFrameData& OpenVDBFrameData = OpenVDBFramesData[FrameIndex];
			OpenVDBFrameData.File->close();
			delete OpenVDBFrameData.File;
		}

		ResultAssets.Add(AnimatedSVTexture);
	}
	else
	{
		// Import as a static sparse volume texture asset.

		FName NewName(InName.ToString() + TEXT("VDB"));
		UStaticSparseVolumeTexture* StaticSVTexture = NewObject<UStaticSparseVolumeTexture>(InParent, UStaticSparseVolumeTexture::StaticClass(), NewName, Flags);

		std::string FileNameStr(TCHAR_TO_ANSI(*Filename));
		openvdb::io::File File(FileNameStr.c_str());

		try
		{
			File.open();
		}
		catch (openvdb::Exception e)
		{
			UE_LOG(LogSparseVolumeTextureFactory, Warning, TEXT("OpenVDB file coult not be opened: %s"), *Filename);
			return nullptr;
		}

		FScopedSlowTask ImportTask(1.0f, LOCTEXT("ImportingVDBStatic", "Importing static OpenVDB"));
		ImportTask.MakeDialog(true);

		openvdb::GridBase::Ptr BaseGrid = nullptr;
		for (openvdb::io::File::NameIterator nameIter = File.beginName();
			nameIter != File.endName(); ++nameIter)
		{
			if (nameIter.gridName() == "density")
			{
				openvdb::GridBase::Ptr QueryGrid = File.readGrid(nameIter.gridName());
				if (QueryGrid->isType<openvdb::FloatGrid>())
				{
					BaseGrid = QueryGrid;
					break;
				}
				break;
			}
		}

		// If we have not found any density map, let's pick up the first float map
		for (openvdb::io::File::NameIterator nameIter = File.beginName();
			nameIter != File.endName(); ++nameIter)
		{
			openvdb::GridBase::Ptr QueryGrid = File.readGrid(nameIter.gridName());
			if (QueryGrid->isType<openvdb::FloatGrid>())
			{
				BaseGrid = QueryGrid;
				break;
			}
		}

		openvdb::FloatGrid::Ptr DensityGrid = openvdb::gridPtrCast<openvdb::FloatGrid>(BaseGrid);

		// Only open float grid for now
		if (DensityGrid == nullptr)
		{
			UE_LOG(LogSparseVolumeTextureFactory, Error, TEXT("Could not convert the first grid to float: %s"), *Filename);
			return nullptr;
		}

		FSparseVolumeRawSource SparseVolumeRawSource;
		if (!ConvertOpenVDBToSparseVolumeTexture(DensityGrid, Filename, SparseVolumeRawSource))
		{
			return nullptr;
		}

		if (ImportTask.ShouldCancel())
		{
			return nullptr;
		}
		ImportTask.EnterProgressFrame(1.0f, LOCTEXT("ConvertingVDBStatic", "Converting static OpenVDB"));

		File.close();

		// Serialise the raw source data into the asset object.
		{
			UE::Serialization::FEditorBulkDataWriter RawDataArchiveWriter(StaticSVTexture->StaticFrame.RawData);
			SparseVolumeRawSource.Serialize(RawDataArchiveWriter);
		}
		ResultAssets.Add(StaticSVTexture);
	}



	// Now notify the system about the imported/updated/created assets
	AdditionalImportedObjects.Reserve(ResultAssets.Num());
	for (UObject* Object : ResultAssets)
	{
		if (Object)
		{
			GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, Object);
			Object->MarkPackageDirty();
			Object->PostEditChange();
			AdditionalImportedObjects.Add(Object);
		}
	}

	return (ResultAssets.Num() > 0) ? ResultAssets[0] : nullptr;

#else // OPENVDB_AVAILABLE

	// SVT_TODO Make sure we can also import on more platforms such as Linux. See SparseVolumeTextureOpenVDB.h
	UE_LOG(LogSparseVolumeTextureFactory, Error, TEXT("Cannot import OpenVDB asset any platform other than Windows."));
	return nullptr;

#endif // OPENVDB_AVAILABLE

}

#endif // WITH_EDITORONLY_DATA

#undef LOCTEXT_NAMESPACE

#include "Serialization/EditorBulkDataWriter.h"
