// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ExrImgMediaReader.h"

#if defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS

#include "IImgMediaReader.h"
#include "IMediaTextureSampleConverter.h"

struct FImgMediaTileSelection;

struct FStructuredBufferPoolItem
{
	/**
	* This is the actual buffer reference that we need to keep after it is locked and until it is unlocked.
	*/
	FStructuredBufferRHIRef BufferRef;

	/** 
	* A pointer to mapped GPU memory.
	*/
	void* MappedBuffer;

	/** 
	* A Gpu fence that identifies if this pool item is available for use again.
	*/
	FGPUFenceRHIRef Fence;

	/**
	* This boolean is used as a flag in combination with fences to indicate if rendering thread 
	* is currently using it.
	*/
	bool bWillBeSignaled = false;
};

/**
* A shared pointer that will be released automatically and returned to UsedPool
*/
typedef TSharedPtr<FStructuredBufferPoolItem, ESPMode::ThreadSafe> FStructuredBufferPoolItemSharedPtr;

/**
 * Implements a reader for EXR image sequences.
 */
class FExrImgMediaReaderGpu : public FExrImgMediaReader
{
public:

	/** Default constructor. */
	FExrImgMediaReaderGpu(const TSharedRef<FImgMediaLoader, ESPMode::ThreadSafe>& InLoader):FExrImgMediaReader(InLoader),
		LastTickedFrameCounter((uint64)-1), bIsShuttingDown(false) {};
	virtual ~FExrImgMediaReaderGpu();

public:

	//~ FExrImgMediaReader interface
	virtual bool ReadFrame(int32 FrameId, int32 MipLevel, const FImgMediaTileSelection& InTileSelection, TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> OutFrame) override;
	
	/**
	* For performance reasons we want to pre-allocate structured buffers to at least the number of concurrent frames.
	*/
	virtual void PreAllocateMemoryPool(int32 NumFrames, const FImgMediaFrameInfo& FrameInfo) override;
	virtual void OnTick() override;

protected:

	/** 
	 * This function reads file in 16 MB chunks and if it detects that
	 * Frame is pending for cancellation stops reading the file and returns false.
	*/
	bool ReadInChunks(uint16* Buffer, const FString& ImagePath, int32 FrameId, const FIntPoint& Dim, int32 BufferSize, int32 PixelSize, int32 NumChannels);

	/**
	 * Get the size of the buffer needed to load in an image.
	 * 
	 * @param Dim Dimensions of the image.
	 * @param NumChannels Number of channels in the image.
	 */
	static SIZE_T GetBufferSize(const FIntPoint& Dim, int32 NumChannels);

public:

	/** Typically we would need the (ImageResolution.x*y)*NumChannels*ChannelSize */
	FStructuredBufferPoolItemSharedPtr AllocateGpuBufferFromPool(uint32 AllocSize, bool bWait = true);

	/** Either return or Add new chunk of memory to the pool based on its size. */
	void ReturnGpuBufferToStagingPool(uint32 AllocSize, FStructuredBufferPoolItem* Buffer);

	/** Transfer from Staging buffer to Memory pool. */
	void TransferFromStagingBuffer();

private:

	/** A critical section used for memory allocation and pool management. */
	FCriticalSection AllocatorCriticalSecion;

	/** Main memory pool from where we are allowed to take buffers. */
	TMultiMap<uint32, FStructuredBufferPoolItem*> MemoryPool;

	/** 
	* This pool could contain potentially in use buffers and every tick it is processed
	* and those buffers that are ready to be used returned back to Main memory pool
	*/
	TMultiMap<uint32, FStructuredBufferPoolItem*> StagingMemoryPool;

	/** Frame that was last ticked so we don't tick more than once. */
	uint64 LastTickedFrameCounter;

	/** A flag indicating this reader is being destroyed, therefore memory should not be returned. */
	bool bIsShuttingDown;
};

FUNC_DECLARE_DELEGATE(FExrConvertBufferCallback, bool, FRHICommandListImmediate& /*RHICmdList*/, FTexture2DRHIRef /*RenderTargetTextureRHI*/)

class FExrMediaTextureSampleConverter: public IMediaTextureSampleConverter
{

public:
	virtual bool Convert(FTexture2DRHIRef& InDstTexture, const FConversionHints& Hints) override;
	virtual ~FExrMediaTextureSampleConverter() {};

public:
	FExrConvertBufferCallback ConvertExrBufferCallback;
};

#endif //defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS

