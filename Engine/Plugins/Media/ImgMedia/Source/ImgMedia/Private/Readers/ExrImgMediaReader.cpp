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
		OutFrame->Format = EMediaTextureSampleFormat::FloatRGBA;
		OutFrame->Data = MakeShareable(Buffer, MoveTemp(BufferDeleter));
		OutFrame->MipMapsPresent = 0;
		OutFrame->Stride = OutFrame->Info.Dim.X * sizeof(unsigned short) * 4;
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
			// Get for our frame/mip level.
			const FString& Image = Loader->GetImagePath(FrameId, CurrentMipLevel);
			FRgbaInputFile InputFile(Image, 2);

			// read frame data
			InputFile.SetFrameBuffer(MipDataPtr, Dim);
			InputFile.ReadPixels(0, Dim.Y - 1);

			OutFrame->MipMapsPresent |= 1 << CurrentMipLevel;
			LevelFoundSoFar = true;
		}

		// Next level.
		MipDataPtr += Dim.X * Dim.Y * sizeof(uint16) * 4;
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
	if (
		FHardwareInfo::GetHardwareInfo(NAME_RHI) == "D3D12"
		&& Info.CompressionName == "Uncompressed" 
		&& CVarEnableUncompressedExrGpuReader.GetValueOnAnyThread()
		)
	{
		return MakeShareable(new FExrImgMediaReaderGpu(InLoader));
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
