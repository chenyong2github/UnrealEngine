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

DECLARE_MEMORY_STAT(TEXT("EXR Reader Pool Memory."), STAT_ExrMediaReaderPoolMem, STATGROUP_ImgMediaPlugin);

static TAutoConsoleVariable<bool> CVarEnableUncompressedExrGpuReader(
	TEXT("r.ExrReadAndProcessOnGPU"),
	true,
	TEXT("Allows reading of Large Uncompressed EXR files directly into Structured Buffer.\n")
	TEXT("and be processed on GPU\n"));


/* FExrImgMediaReader structors
 *****************************************************************************/

FExrImgMediaReader::FExrImgMediaReader()
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


bool FExrImgMediaReader::ReadFrame(const FString& ImagePath, TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> OutFrame, int32 FrameId)
{
	FRgbaInputFile InputFile(ImagePath, 2);
	
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
	const SIZE_T BufferSize = Dim.X * Dim.Y * sizeof(uint16) * 4;
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

	// read frame data
	InputFile.SetFrameBuffer(Buffer, Dim);
	InputFile.ReadPixels(0, Dim.Y - 1);

	OutFrame->Data = MakeShareable(Buffer, MoveTemp(BufferDeleter));
	OutFrame->Format = EMediaTextureSampleFormat::FloatRGBA;
	OutFrame->Stride = Dim.X * sizeof(unsigned short) * 4;

	return true;
}

void FExrImgMediaReader::CancelFrame(int32 FrameNumber)
{
	UE_LOG(LogImgMedia, Warning, TEXT("Reader %p: Canceling Frame. %i"), this, FrameNumber);
	FScopeLock RegionScopeLock(&CanceledFramesCriticalSection);
	CanceledFrames.Add(FrameNumber);
}

/** Gets reader type (GPU vs CPU) depending on size of EXR and its compression. */
TSharedPtr<IImgMediaReader, ESPMode::ThreadSafe> FExrImgMediaReader::GetReader(FString FirstImageInSequencePath)
{
#if defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS
	FRgbaInputFile InputFile(FirstImageInSequencePath, 2);
	FImgMediaFrameInfo Info;

	if (!GetInfo(InputFile, Info))
	{
		return MakeShareable(new FExrImgMediaReader);
	}
	
	// Check GetCompressionName of OpenExrWrapper for other compression names.
	if (
		FHardwareInfo::GetHardwareInfo(NAME_RHI) == "D3D12"
		&& Info.CompressionName == "Uncompressed" 
		&& CVarEnableUncompressedExrGpuReader.GetValueOnAnyThread()
		)
	{
		return MakeShareable(new FExrImgMediaReaderGpu);
	}
#endif
	return MakeShareable(new FExrImgMediaReader);
}

/* FExrImgMediaReader implementation
 *****************************************************************************/

bool FExrImgMediaReader::GetInfo(FRgbaInputFile& InputFile, FImgMediaFrameInfo& OutInfo)
{
	OutInfo.CompressionName = InputFile.GetCompressionName();
	OutInfo.Dim = InputFile.GetDataWindow();
	OutInfo.FormatName = TEXT("EXR");
	OutInfo.FrameRate = InputFile.GetFrameRate(ImgMedia::DefaultFrameRate);
	OutInfo.Srgb = false;
	OutInfo.UncompressedSize = InputFile.GetUncompressedSize();
	OutInfo.NumChannels = InputFile.GetNumChannels();

	return (OutInfo.UncompressedSize > 0) && (OutInfo.Dim.GetMin() > 0);
}


#endif //IMGMEDIA_EXR_SUPPORTED_PLATFORM
