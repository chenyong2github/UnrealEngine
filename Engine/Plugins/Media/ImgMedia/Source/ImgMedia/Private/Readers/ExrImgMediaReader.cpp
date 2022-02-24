// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExrImgMediaReader.h"
#include "ExrImgMediaReaderGpu.h"

#if IMGMEDIA_EXR_SUPPORTED_PLATFORM

#include "Async/Async.h"
#include "Misc/Paths.h"
#include "OpenExrWrapper.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "HardwareInfo.h"
#include "ImgMediaLoader.h"
#include "ImgMediaMipMapInfo.h"

DECLARE_MEMORY_STAT(TEXT("EXR Reader Pool Memory."), STAT_ExrMediaReaderPoolMem, STATGROUP_ImgMediaPlugin);

static TAutoConsoleVariable<bool> CVarEnableUncompressedExrGpuReader(
	TEXT("r.ExrReadAndProcessOnGPU"),
	true,
	TEXT("Allows reading of Large Uncompressed EXR files directly into Structured Buffer.\n")
	TEXT("and be processed on GPU\n"));


/* FExrImgMediaReader structors
 *****************************************************************************/

FExrImgMediaReader::FExrImgMediaReader(const TSharedRef<FImgMediaLoader, ESPMode::ThreadSafe>& InLoader)
	: LoaderPtr(InLoader)
{
	const UImgMediaSettings* Settings = GetDefault<UImgMediaSettings>();
	
	FOpenExr::SetGlobalThreadCount(Settings->ExrDecoderThreads == 0
		? FPlatformMisc::NumberOfCoresIncludingHyperthreads()
		: Settings->ExrDecoderThreads);
}

FExrImgMediaReader::~FExrImgMediaReader()
{
	FScopeLock RegionScopeLock(&CanceledFramesCriticalSection);
	CanceledFrames.Reset();
}

/* FExrImgMediaReader interface
 *****************************************************************************/

bool FExrImgMediaReader::GetFrameInfo(const FString& ImagePath, FImgMediaFrameInfo& OutInfo)
{

	FRgbaInputFile InputFile(ImagePath, 2);


	return GetInfo(InputFile, OutInfo);
}


bool FExrImgMediaReader::ReadFrame(int32 FrameId, int32 MipLevel, const FImgMediaTileSelection& InTileSelection, TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> OutFrame)
{
	TSharedPtr<FImgMediaLoader, ESPMode::ThreadSafe> Loader = LoaderPtr.Pin();
	if (Loader.IsValid() == false)
	{
		return false;
	}
	
	// Get tile info.
	int32 NumTilesX = Loader->GetNumTilesX();
	int32 NumTilesY = Loader->GetNumTilesY();
	bool bHasTiles = (NumTilesX * NumTilesY) > 1;
	int32 StartTileX = InTileSelection.TopLeftX;
	int32 StartTileY = InTileSelection.TopLeftY;
	int32 EndTileX = FMath::Min((int32)InTileSelection.BottomRightX, NumTilesX);
	int32 EndTileY = FMath::Min((int32)InTileSelection.BottomRightY, NumTilesY);

	int32 BytesPerPixelPerChannel = sizeof(uint16);
	int32 NumChannels = 4;
	int32 BytesPerPixel = BytesPerPixelPerChannel * NumChannels;

	// Do we already have our buffer?
	if (OutFrame->Data.IsValid() == false)
	{
		// Nope. Create it.
		const FString& LargestImage = Loader->GetImagePath(FrameId, 0);
		FRgbaInputFile InputFile(LargestImage, 2);
		if (!GetInfo(InputFile, OutFrame->Info))
		{
			return false;
		}

		// If we have tiles, then this is the size of just a tile, so multiply to get the full size.
		OutFrame->Info.Dim.X *= NumTilesX;
		OutFrame->Info.Dim.Y *= NumTilesY;

		const FIntPoint& Dim = OutFrame->Info.Dim;

		if (Dim.GetMin() <= 0)
		{
			return false;
		}

		// allocate frame buffer
		SIZE_T BufferSize = GetMipBufferTotalSize(Dim);
		void* Buffer = FMemory::Malloc(BufferSize, PLATFORM_CACHE_LINE_SIZE);

		auto BufferDeleter = [BufferSize](void* ObjectToDelete) {
#if USE_IMGMEDIA_DEALLOC_POOL
			if (FQueuedThreadPool* ImgMediaThreadPoolSlow = GetImgMediaThreadPoolSlow())
			{
				// free buffers on the thread pool, because memory allocators may perform
				// expensive operations, such as filling the memory with debug values
				TFunction<void()> FreeBufferTask = [ObjectToDelete]()
				{
					FMemory::Free(ObjectToDelete);
				};
				AsyncPool(*ImgMediaThreadPoolSlow, FreeBufferTask);
			}
			else
			{
				FMemory::Free(ObjectToDelete);
			}
#else
			FMemory::Free(ObjectToDelete);
#endif
		};
		
		// The EXR RGBA interface only outputs RGBA data.
		OutFrame->Format = EMediaTextureSampleFormat::FloatRGBA;
		OutFrame->Data = MakeShareable(Buffer, MoveTemp(BufferDeleter));
		OutFrame->MipMapsPresent = 0;
		OutFrame->Stride = OutFrame->Info.Dim.X * BytesPerPixel;
	}

	// Loop over all mips.
	uint8* MipDataPtr = (uint8*)(OutFrame->Data.Get());
	FIntPoint Dim = OutFrame->Info.Dim;
	bool LevelFoundSoFar = false;
	int32 NumMipLevels = Loader->GetNumMipLevels();
	for (int32 CurrentMipLevel = 0; CurrentMipLevel < NumMipLevels; ++CurrentMipLevel)
	{
		// Do we want to read in this mip?
		bool IsThisLevelPresent = (OutFrame->MipMapsPresent & (1 << CurrentMipLevel)) != 0;
		bool ReadThisMip = (CurrentMipLevel >= MipLevel) &&
			(IsThisLevelPresent == false);
		
		if (ReadThisMip)
		{
			FString Image = Loader->GetImagePath(FrameId, CurrentMipLevel);
			FString BaseImage;
			if (bHasTiles)
			{
				// Remove "_x0_y0.exr" so we can add on the correct name for the tile we want.
				BaseImage = Image.LeftChop(10);
			}

			int32 TileWidth = Dim.X / NumTilesX;
			int32 TileHeight = Dim.Y / NumTilesY;
			// The offset into the frame buffer for each row of tiles.
			// Total width * height of a tile * bytes per pixel.
			int32 FrameBufferOffsetPerTileY = Dim.X * TileHeight * BytesPerPixel;
			int32 FrameBufferOffsetY = FrameBufferOffsetPerTileY * StartTileY;
			
			for (int32 TileY = StartTileY; TileY < EndTileY; TileY++)
			{
				// The offset into the frame buffer for each column of tiles.
				// Tile width * bytes per pixel.
				int32 FrameBufferOffsetPerTileX = TileWidth * BytesPerPixel;
				int32 FrameBufferOffsetX = FrameBufferOffsetPerTileX * StartTileX;
				for (int32 TileX = StartTileX; TileX < EndTileX; TileX++)
				{
					// Get for our frame/mip level.
					if (bHasTiles)
					{
						Image = FString::Printf(TEXT("%s_x%d_y%d.exr"), *BaseImage, TileX, TileY);
					}
					FRgbaInputFile InputFile(Image, 2);
					if (InputFile.HasInputFile())
					{
						// read frame data
						InputFile.SetFrameBuffer(MipDataPtr + FrameBufferOffsetX + FrameBufferOffsetY, Dim);
						InputFile.ReadPixels(0, TileHeight - 1);

						FrameBufferOffsetX += FrameBufferOffsetPerTileX;
						OutFrame->MipMapsPresent |= 1 << CurrentMipLevel;
						LevelFoundSoFar = true;
					}
					else
					{
						UE_LOG(LogImgMedia, Error, TEXT("Could not load %s"), *Image);
					}
				}
				
				FrameBufferOffsetY += FrameBufferOffsetPerTileY;
			}
		}

		// Next level.
		MipDataPtr += Dim.X * Dim.Y * BytesPerPixel;
		Dim /= 2;
		if (IsThisLevelPresent)
		{
			LevelFoundSoFar = true;
		}
	}

	return true;
}

void FExrImgMediaReader::CancelFrame(int32 FrameNumber)
{
	UE_LOG(LogImgMedia, Verbose, TEXT("Reader %p: Canceling Frame. %i"), this, FrameNumber);
	FScopeLock RegionScopeLock(&CanceledFramesCriticalSection);
	CanceledFrames.Add(FrameNumber);
}

/** Gets reader type (GPU vs CPU) depending on size of EXR and its compression. */
TSharedPtr<IImgMediaReader, ESPMode::ThreadSafe> FExrImgMediaReader::GetReader(const TSharedRef <FImgMediaLoader, ESPMode::ThreadSafe>& InLoader, FString FirstImageInSequencePath)
{
#if defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS
	FRgbaInputFile InputFile(FirstImageInSequencePath, 2);
	if (InputFile.HasInputFile() == false)
	{
		TSharedPtr<IImgMediaReader, ESPMode::ThreadSafe> Ptr;
		return Ptr;
	}
	
	FImgMediaFrameInfo Info;

	if (!GetInfo(InputFile, Info))
	{
		return MakeShareable(new FExrImgMediaReader(InLoader));
	}
	
	// Check GetCompressionName of OpenExrWrapper for other compression names.
	if (GDynamicRHI && GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::D3D12
		&& Info.CompressionName == "Uncompressed" 
		&& CVarEnableUncompressedExrGpuReader.GetValueOnAnyThread()
		)
	{
		return MakeShared<FExrImgMediaReaderGpu, ESPMode::ThreadSafe>(InLoader);
	}
#endif
	return MakeShareable(new FExrImgMediaReader(InLoader));
}

/* FExrImgMediaReader implementation
 *****************************************************************************/

bool FExrImgMediaReader::GetInfo(FRgbaInputFile& InputFile, FImgMediaFrameInfo& OutInfo)
{
	if (InputFile.HasInputFile() == false)
	{
		return false;
	}

	OutInfo.CompressionName = InputFile.GetCompressionName();
	OutInfo.Dim = InputFile.GetDataWindow();
	OutInfo.FormatName = TEXT("EXR");
	OutInfo.FrameRate = InputFile.GetFrameRate(ImgMedia::DefaultFrameRate);
	OutInfo.Srgb = false;
	OutInfo.UncompressedSize = InputFile.GetUncompressedSize();
	OutInfo.NumChannels = InputFile.GetNumChannels();

	return (OutInfo.UncompressedSize > 0) && (OutInfo.Dim.GetMin() > 0);
}

SIZE_T FExrImgMediaReader::GetMipBufferTotalSize(FIntPoint Dim)
{
	SIZE_T Size = ((Dim.X * Dim.Y * 4) / 3) * sizeof(uint16) * 4;
	
	return Size;
}

#endif //IMGMEDIA_EXR_SUPPORTED_PLATFORM
