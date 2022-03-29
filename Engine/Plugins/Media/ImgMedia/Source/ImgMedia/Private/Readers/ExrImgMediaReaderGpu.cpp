// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExrImgMediaReaderGpu.h"

#if defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS

#include "Async/Async.h"
#include "Misc/Paths.h"
#include "OpenExrWrapper.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "ExrReaderGpu.h"
#include "RHICommandList.h"
#include "ExrSwizzlingShader.h"
#include "ImgMedia/Public/ImgMediaMipMapInfo.h"
#include "SceneUtils.h"


#include "Engine/TextureRenderTarget2D.h"
#include "GlobalShader.h"
#include "CommonRenderResources.h"
#include "ImgMediaLoader.h"
#include "RHIStaticStates.h"
#include "SceneUtils.h"
#include "ShaderParameterUtils.h"
#include "TextureResource.h"
#include "ScreenPass.h"

#include "ID3D12DynamicRHI.h"


DECLARE_GPU_STAT_NAMED(ExrImgMediaReaderGpu, TEXT("ExrImgMediaReaderGpu"));

namespace {

	/* Padding in bits that is added at the beginning of each scanline */
	const uint16 CExrRowPadding = 8;

	/* Padding in bits that is added at the beginning of each tile */
	const uint16 CExrTilePadding = 20;

	/** This function is similar to DrawScreenPass in OpenColorIODisplayExtension.cpp except it is catered for Viewless texture rendering. */
	template<typename TSetupFunction>
	void DrawScreenPass(
		FRHICommandListImmediate& RHICmdList,
		const FIntPoint& OutputResolution,
		const FIntRect& Viewport,
		const FScreenPassPipelineState& PipelineState,
		TSetupFunction SetupFunction)
	{
		RHICmdList.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.f, Viewport.Max.X, Viewport.Max.Y, 1.0f);

		SetScreenPassPipelineState(RHICmdList, PipelineState);

		// Setting up buffers.
		SetupFunction(RHICmdList);

		EDrawRectangleFlags DrawRectangleFlags = EDRF_UseTriangleOptimization;

		DrawPostProcessPass(
			RHICmdList,
			0, 0, OutputResolution.X, OutputResolution.Y,
			Viewport.Min.X, Viewport.Min.Y, Viewport.Width(), Viewport.Height(),
			OutputResolution,
			OutputResolution,
			PipelineState.VertexShader,
			INDEX_NONE,
			false,
			DrawRectangleFlags);
	}
}


/* FExrImgMediaReaderGpu structors
 *****************************************************************************/

FExrImgMediaReaderGpu::~FExrImgMediaReaderGpu()
{
	// A signal that tells all buffers that are stored in shared references not to return to the pool
	// but delete instead.
	bIsShuttingDown = true;

	// Making sure that all Used memory is processed first and returned into memory pool
	TransferFromStagingBuffer();

	// Unlock all buffers so that these will release.
	volatile bool bUnlocked = false;
	ENQUEUE_RENDER_COMMAND(DeletePooledBuffers)([this, &bUnlocked](FRHICommandListImmediate& RHICmdList)
	{
		FScopeLock ScopeLock(&AllocatorCriticalSecion);
		SCOPED_DRAW_EVENT(RHICmdList, FExrImgMediaReaderGpu_ReleaseMemoryPool);
		TArray<uint32> KeysForIteration;
		MemoryPool.GetKeys(KeysForIteration);
		for (uint32 Key : KeysForIteration)
		{
			TArray<FStructuredBufferPoolItem*> AllValues;
			MemoryPool.MultiFind(Key, AllValues);
			for (FStructuredBufferPoolItem* MemoryPoolItem : AllValues)
			{
				// Check if fence has signaled.
				check(!MemoryPoolItem->bWillBeSignaled || MemoryPoolItem->Fence->Poll());
				{
					RHIUnlockBuffer(MemoryPoolItem->BufferRef);
					delete MemoryPoolItem;
				}
			}
		}
		MemoryPool.Reset();
		bUnlocked = true;
	});

	// Wait until unlocking is complete.
	while (!bUnlocked)
	{
		FPlatformProcess::Sleep(0.01f);
	}
}


/* FExrImgMediaReaderGpu interface
 *****************************************************************************/

bool FExrImgMediaReaderGpu::ReadFrame(int32 FrameId, const TMap<int32, FImgMediaTileSelection>& InMipTiles, TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> OutFrame)
{
	// Fall back to cpu?
	if (bFallBackToCPU)
	{
		return FExrImgMediaReader::ReadFrame(FrameId, InMipTiles, OutFrame);
	}

	TSharedPtr<FImgMediaLoader, ESPMode::ThreadSafe> Loader = LoaderPtr.Pin();
	if (Loader.IsValid() == false)
	{
		return false;
	}


	const FString& LargestImagePath = Loader->GetImagePath(FrameId, 0);

	if (!GetInfo(LargestImagePath, OutFrame->Info))
	{
		return false;
	}

	// Get tile info.
	bool bHasTiles = OutFrame->Info.bHasTiles;

	const FIntPoint& FullResolution = OutFrame->Info.Dim;

	const FIntPoint TileDim = OutFrame->Info.TileDimensions;

	if (FullResolution.GetMin() <= 0)
	{
		return false;
	}

	TMap<int32, FIntRect> Viewports;

	const int32 NumChannels = OutFrame->Info.NumChannels;
	const int32 PixelSize = sizeof(uint16) * NumChannels;

	TArray<FStructuredBufferPoolItemSharedPtr> BufferDataArray;
	BufferDataArray.AddDefaulted(FImgMediaLoader::MAX_MIPMAP_LEVELS);

	int32 NumMipLevels = Loader->GetNumMipLevels();
	{
		// Loop over all mips.
		FIntPoint CurrentMipDim = FullResolution;

		for (const TPair<int32, FImgMediaTileSelection>& TilesPerMip : InMipTiles)
		{
			const int32 CurrentMipLevel = TilesPerMip.Key;
			const FImgMediaTileSelection& CurrentTileSelection = TilesPerMip.Value;

			bool ReadThisMip = true;
			// Avoid reads if the cached frame already contains the current tiles for this mip level.
			if (const FImgMediaTileSelection* CachedSelection = OutFrame->MipTilesPresent.Find(CurrentMipLevel))
			{
				ReadThisMip = !CachedSelection->Contains(CurrentTileSelection);
			}
			
			// Next mip level.
			int MipLevelDiv = 1 << CurrentMipLevel;
			CurrentMipDim = FullResolution / MipLevelDiv;

			if (ReadThisMip)
			{
				FIntRect& Viewport = Viewports.Add(CurrentMipLevel);
				Viewport.Min = FIntPoint(TileDim.X * CurrentTileSelection.TopLeftX, TileDim.X * CurrentTileSelection.TopLeftY);
				Viewport.Max = FIntPoint(TileDim.Y * CurrentTileSelection.BottomRightX, TileDim.Y * CurrentTileSelection.BottomRightY);
				Viewport.Clip(FIntRect(FIntPoint::ZeroValue, CurrentMipDim));

				const SIZE_T BufferSize = GetBufferSize(CurrentMipDim, NumChannels, bHasTiles, OutFrame->Info.NumTiles / MipLevelDiv);
				FStructuredBufferPoolItemSharedPtr& BufferData = BufferDataArray[CurrentMipLevel];
				BufferData = AllocateGpuBufferFromPool(BufferSize);
				uint16* MipDataPtr = static_cast<uint16*>(BufferData->MappedBuffer);

				// Get for our frame/mip level.
				FString ImagePath = Loader->GetImagePath(FrameId, CurrentMipLevel);
				
				EReadResult ReadResult = Fail;

				FOpenExrHeaderReader InputTileFile(ImagePath);
				if (InputTileFile.HasInputFile())
				{
					// read frame data
					if (bHasTiles)
					{
						FIntRect TileRegion = FIntRect(
							(int32)CurrentTileSelection.TopLeftX,
							(int32)CurrentTileSelection.TopLeftY,
							FMath::Min((int32)CurrentTileSelection.BottomRightX, FMath::CeilToInt(float(OutFrame->Info.NumTiles.X) / MipLevelDiv)),
							FMath::Min((int32)CurrentTileSelection.BottomRightY, FMath::CeilToInt(float(OutFrame->Info.NumTiles.Y) / MipLevelDiv)));
						FIntPoint NumTiles = OutFrame->Info.NumTiles / MipLevelDiv;

						ReadResult = ReadTiles(MipDataPtr, ImagePath, FrameId, TileRegion, NumTiles, TileDim, PixelSize);
					}
					else
					{
						ReadResult = ReadInChunks(MipDataPtr, ImagePath, FrameId, CurrentMipDim, BufferSize);
					}

					if (ReadResult != Fail)
					{
						OutFrame->MipTilesPresent.Emplace(CurrentMipLevel, CurrentTileSelection);
					}
				}
				else
				{
					UE_LOG(LogImgMedia, Error, TEXT("Could not load %s"), *ImagePath);
					return false;
				}
				if (ReadResult == Fail)
				{
					// Check if we have a compressed file.
					if (OutFrame->Info.CompressionName != "Uncompressed")
					{
						UE_LOG(LogImgMedia, Error, TEXT("GPU Reader cannot read compressed file %s."), *ImagePath);
						UE_LOG(LogImgMedia, Error, TEXT("Compressed and uncompressed files should not be mixed in a single sequence."));
					}

					// Fall back to CPU.
					bFallBackToCPU = true;
					return FExrImgMediaReader::ReadFrame(FrameId, InMipTiles, OutFrame);
				}
			}
		}
	}
	OutFrame->Format = NumChannels <= 3 ? EMediaTextureSampleFormat::FloatRGB : EMediaTextureSampleFormat::FloatRGBA;
	OutFrame->Stride = FullResolution.X * PixelSize;

	OutFrame->SampleConverter = CreateSampleConverter(MoveTemp(BufferDataArray), FullResolution, TileDim, Viewports, NumChannels, NumMipLevels, bHasTiles);

	UE_LOG(LogImgMedia, Verbose, TEXT("Reader %p: Read Pixels Complete. %i"), this, FrameId);
	return true;
}

void FExrImgMediaReaderGpu::PreAllocateMemoryPool(int32 NumFrames, const FImgMediaFrameInfo& FrameInfo)
{
	SIZE_T AllocSize = GetBufferSize(FrameInfo.Dim, FrameInfo.NumChannels, FrameInfo.bHasTiles, FrameInfo.NumTiles);
	for (int32 FrameCacheNum = 0; FrameCacheNum < NumFrames; FrameCacheNum++)
	{
		AllocateGpuBufferFromPool(AllocSize, FrameCacheNum == NumFrames - 1);
	}
}


void FExrImgMediaReaderGpu::OnTick()
{
	// Only tick once per frame.
	if (LastTickedFrameCounter != GFrameCounter)
	{
		LastTickedFrameCounter = GFrameCounter;

		TransferFromStagingBuffer();
	}
}

/* FExrImgMediaReaderGpu implementation
 *****************************************************************************/

FExrImgMediaReaderGpu::EReadResult FExrImgMediaReaderGpu::ReadInChunks(uint16* Buffer, const FString& ImagePath, int32 FrameId, const FIntPoint& Dim, int32 BufferSize)
{
	EReadResult bResult = Success;

	// Chunks are of 16 MB
	const int32 ChunkSize = 0xF42400;
	const int32 Remainder = BufferSize % ChunkSize;
	const int32 NumChunks = (BufferSize - Remainder) / ChunkSize;
	int32 CurrentBufferPos = 0;
	FExrReader ChunkReader;


	if (!ChunkReader.OpenExrAndPrepareForPixelReading(ImagePath, Dim.Y))
	{
		return Fail;
	}

	for (int32 Row = 0; Row <= NumChunks; Row++)
	{
		int32 Step = Row == NumChunks ? Remainder : ChunkSize;
		if (Step == 0)
		{
			break;
		}

		// Check to see if the frame was canceled.
		{
			FScopeLock RegionScopeLock(&CanceledFramesCriticalSection);
			if (CanceledFrames.Remove(FrameId) > 0)
			{
				UE_LOG(LogImgMedia, Warning, TEXT("Reader %p: Canceling Frame %i At chunk # %i"), this, FrameId, Row);
				bResult = Cancelled;
				break;
			}
		}

		if (!ChunkReader.ReadExrImageChunk(reinterpret_cast<char*>(Buffer) + CurrentBufferPos, Step))
		{
			bResult = Fail;
			break;
		}
		CurrentBufferPos += Step;
	}

	if (!ChunkReader.CloseExrFile())
	{
		return Fail;
	}

	return bResult;
}

FExrImgMediaReaderGpu::EReadResult FExrImgMediaReaderGpu::ReadTiles(uint16* Buffer, const FString& ImagePath, int32 FrameId, const FIntRect& TileRegion, const FIntPoint& FullTexDimInTiles, const FIntPoint& TileDim, const int32 PixelSize)
{
	EReadResult bResult = Success;

	// Chunks are of 16 MB
	int32 CurrentBufferPos = 0;
	FExrReader ChunkReader;


	if (!ChunkReader.OpenExrAndPrepareForPixelReading(ImagePath, FullTexDimInTiles.X * FullTexDimInTiles.Y))
	{
		return Fail;
	}

	for (int32 TileRow = TileRegion.Min.Y; TileRow < TileRegion.Max.Y; TileRow++)
	{
		// Check to see if the frame was canceled.
		{
			FScopeLock RegionScopeLock(&CanceledFramesCriticalSection);
			if (CanceledFrames.Remove(FrameId) > 0)
			{
				UE_LOG(LogImgMedia, Warning, TEXT("Reader %p: Canceling Frame %i At tile row # %i"), this, FrameId, TileRow);
				bResult = Cancelled;
				break;
			}
		}

		const int32 TileByteStride = PixelSize * TileDim.X * TileDim.Y + CExrTilePadding;
		const int StartTileIndex = TileRow * FullTexDimInTiles.X + TileRegion.Min.X;
		const int EndTileIndex = TileRow * FullTexDimInTiles.X + TileRegion.Max.X;
		const int BytesToRead = (EndTileIndex - StartTileIndex) * TileByteStride;

		ChunkReader.SeekTileWithinFile(StartTileIndex, FullTexDimInTiles, CurrentBufferPos);

		if (!ChunkReader.ReadExrImageChunk(reinterpret_cast<char*>(Buffer) + CurrentBufferPos, BytesToRead))
		{
			bResult = Fail;
			break;
		}
	}

	if (!ChunkReader.CloseExrFile())
	{
		return Fail;
	}

	return bResult;
}

SIZE_T FExrImgMediaReaderGpu::GetBufferSize(const FIntPoint& Dim, int32 NumChannels, bool bHasTiles, const FIntPoint& TileNum)
{
	if (!bHasTiles)
	{
		/** 
		* Reading scanlines.
		* 
		* At the beginning of each row of B G R channel planes there is 2x4 byte data that has information
		* about number of pixels in the current row and row's number.
		*/
		SIZE_T BufferSize = Dim.X * Dim.Y * sizeof(uint16) * NumChannels + Dim.Y * CExrRowPadding;
		return BufferSize;
	}
	else
	{
		/** 
		* Reading tiles.
		* 
		* At the beginning of each tile of B G R channel planes there is 10 byte data that has information
		* about number contents of tiles.
		*/
		SIZE_T BufferSize = Dim.X * Dim.Y * sizeof(uint16) * NumChannels + TileNum.X * TileNum.Y * CExrTilePadding;
		return BufferSize;
	}

}

TSharedPtr<IMediaTextureSampleConverter, ESPMode::ThreadSafe> FExrImgMediaReaderGpu::CreateSampleConverter
	( TArray<FStructuredBufferPoolItemSharedPtr>&& BufferDataArray
	, const FIntPoint& FullResolution
	, const FIntPoint& TileDim
	, const TMap<int32, FIntRect>& Viewports
	, const int32 NumChannels
	, const int32 NumMipLevels
	, const bool bHasTiles)
{
	auto RenderThreadSwizzler = [this, BufferDataArray, FullResolution, TileDim, Viewports, NumChannels, NumMipLevels, bHasTiles](FRHICommandListImmediate& RHICmdList, FTexture2DRHIRef RenderTargetTextureRHI)->bool
	{

		SCOPED_DRAW_EVENT(RHICmdList, FExrImgMediaReaderGpu_Convert);
		SCOPED_GPU_STAT(RHICmdList, ExrImgMediaReaderGpu);

		FIntPoint Dim = FullResolution;
		for (int32 MipLevel = 0; MipLevel < NumMipLevels; ++MipLevel)
		{
			int MipLevelDiv = 1 << MipLevel;
			Dim = FullResolution / MipLevelDiv;

			FStructuredBufferPoolItemSharedPtr BufferData = BufferDataArray[MipLevel];
			if (BufferData.IsValid())
			{
				if (!BufferData->BufferRef->IsValid())
				{
					continue;
				}

				FIntRect Viewport = Viewports[MipLevel];

				// This flag will indicate that we should wait for poll to complete.
				BufferData->bWillBeSignaled = true;

				FRHIRenderPassInfo RPInfo(RenderTargetTextureRHI, ERenderTargetActions::DontLoad_Store, nullptr, MipLevel);
				RHICmdList.BeginRenderPass(RPInfo, TEXT("ExrTextureSwizzle"));

				FExrSwizzlePS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FExrSwizzlePS::FRgbaSwizzle>(NumChannels);
				PermutationVector.Set<FExrSwizzlePS::FSwizzleTiles>(bHasTiles);
				FExrSwizzlePS::FParameters Parameters = FExrSwizzlePS::FParameters();
				Parameters.TextureSize = Dim;
				Parameters.TileSize = TileDim;
				if (bHasTiles)
				{
					Parameters.NumTiles = Dim / TileDim;
				}

				Parameters.UnswizzledBuffer = RHICreateShaderResourceView(BufferData->BufferRef);

				FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

				TShaderMapRef<FExrSwizzleVS> SwizzleShaderVS(ShaderMap);
				TShaderMapRef<FExrSwizzlePS> SwizzleShaderPS(ShaderMap, PermutationVector);

				FScreenPassPipelineState PipelineState(SwizzleShaderVS, SwizzleShaderPS, TStaticBlendState<>::GetRHI(), TStaticDepthStencilState<false, CF_Always>::GetRHI());
				DrawScreenPass(RHICmdList, Dim, Viewport, PipelineState, [&](FRHICommandListImmediate& RHICmdList)
					{
						SetShaderParameters(RHICmdList, SwizzleShaderPS, SwizzleShaderPS.GetPixelShader(), Parameters);
					});

				// Resolve render target.
				RHICmdList.EndRenderPass();

				// Mark this render command for this buffer as complete, so we can poll it and transfer later.
				RHICmdList.WriteGPUFence(BufferData->Fence);
			}

			
		}

		//Doesn't need further conversion so returning false.
		return false;
	};

	FExrMediaTextureSampleConverter* SampleConverter = new FExrMediaTextureSampleConverter();
	SampleConverter->ConvertExrBufferCallback = FExrConvertBufferCallback::CreateLambda(RenderThreadSwizzler);

	return MakeShareable(SampleConverter);
}

FStructuredBufferPoolItemSharedPtr FExrImgMediaReaderGpu::AllocateGpuBufferFromPool(uint32 AllocSize, bool bWait)
{
	// This function is attached to the shared pointer and is used to return any allocated memory to staging pool.
	auto BufferDeleter = [AllocSize](FStructuredBufferPoolItem* ObjectToDelete) {
		ReturnGpuBufferToStagingPool(AllocSize, ObjectToDelete);
	};

	// Buffer that ends up being returned out of this function.
	FStructuredBufferPoolItemSharedPtr AllocatedBuffer;

	{
		FScopeLock ScopeLock(&AllocatorCriticalSecion);
		FStructuredBufferPoolItem** FoundBuffer = MemoryPool.Find(AllocSize);
		if (FoundBuffer)
		{
			AllocatedBuffer = MakeShareable(*FoundBuffer, MoveTemp(BufferDeleter));
			MemoryPool.Remove(AllocSize, *FoundBuffer);
		}
	}

	if (!AllocatedBuffer)
	{
		// This boolean value is used to wait until render thread finishes buffer initialization.
		volatile bool bInitDone = false;
		{
			AllocatedBuffer = MakeShareable(new FStructuredBufferPoolItem(), MoveTemp(BufferDeleter));
			AllocatedBuffer->Reader = AsShared();

			// Allocate and unlock the structured buffer on render thread.
			ENQUEUE_RENDER_COMMAND(CreatePooledBuffer)([AllocatedBuffer, AllocSize, &bInitDone, this, bWait](FRHICommandListImmediate& RHICmdList)
			{
				FScopeLock ScopeLock(&AllocatorCriticalSecion);
				SCOPED_DRAW_EVENT(RHICmdList, FExrImgMediaReaderGpu_AllocateBuffer);
				FRHIResourceCreateInfo CreateInfo(TEXT("FExrImgMediaReaderGpu"));
				AllocatedBuffer->BufferRef = RHICreateStructuredBuffer(sizeof(uint16) * 2., AllocSize, BUF_ShaderResource | BUF_Dynamic | BUF_FastVRAM, CreateInfo);
				AllocatedBuffer->MappedBuffer = static_cast<uint16*>(RHILockBuffer(AllocatedBuffer->BufferRef, 0, AllocSize, RLM_WriteOnly));
				AllocatedBuffer->Fence = RHICreateGPUFence(TEXT("BufferNoLongerInUseFence"));
				if (bWait)
				{
					bInitDone = true;
				}
			});
		}

		/** Wait until buffer is initialized. */
		while (!bInitDone && bWait)
		{
			FPlatformProcess::Sleep(0.01f);
		}

	}

	// This buffer will be automatically processed and returned to StagingMemoryPool once nothing keeps reference to it.
	return AllocatedBuffer;
}

void FExrImgMediaReaderGpu::ReturnGpuBufferToStagingPool(uint32 AllocSize, FStructuredBufferPoolItem* Buffer)
{
	TSharedPtr< FExrImgMediaReaderGpu, ESPMode::ThreadSafe> Reader = Buffer->Reader.Pin();

	// If reader is being deleted, we don't need to return the memory into staging buffer and instead should delete it.
	if ((Reader.IsValid() == false) || (Reader->bIsShuttingDown))
	{
		ENQUEUE_RENDER_COMMAND(DeletePooledBuffers)([Buffer](FRHICommandListImmediate& RHICmdList)
		{
			SCOPED_DRAW_EVENT(RHICmdList, FExrImgMediaReaderGpu_ReleaseBuffer);

			// By this point we don't need a lock because the destructor was already called and it 
			// is guaranteed that this buffer is no longer used anywhere else.
			RHIUnlockBuffer(Buffer->BufferRef);
			delete Buffer;
		});
	}
	else
	{
		FScopeLock ScopeLock(&Reader->AllocatorCriticalSecion);

		// We don't need to process this pooled buffer if the Reader is being destroyed.
		Reader->StagingMemoryPool.Add(AllocSize, Buffer);
	}

}

void FExrImgMediaReaderGpu::TransferFromStagingBuffer()
{
	ENQUEUE_RENDER_COMMAND(CreatePooledBuffer)([&, this](FRHICommandListImmediate& RHICmdList)
	{
		FScopeLock ScopeLock(&AllocatorCriticalSecion);
		SCOPED_DRAW_EVENT(RHICmdList, FExrImgMediaReaderGpu_TransferFromStagingBuffer);

		TArray<uint32> KeysForIteration;
		StagingMemoryPool.GetKeys(KeysForIteration);
		for (uint32 Key : KeysForIteration)
		{
			TArray<FStructuredBufferPoolItem*> AllValues;
			StagingMemoryPool.MultiFind(Key, AllValues);
			for (FStructuredBufferPoolItem* MemoryPoolItem : AllValues)
			{
				// Check if fence has signaled. Or otherwise if we are waiting for signal to come through.
				if (MemoryPoolItem->Fence->Poll() || !MemoryPoolItem->bWillBeSignaled)
				{
					// If buffer was in use but the fence signaled we need to reset bWillBeSignaled flag.
					MemoryPoolItem->bWillBeSignaled = false;

					StagingMemoryPool.Remove(Key, MemoryPoolItem);
					MemoryPool.Add(Key, MemoryPoolItem);
				}
			}
		}
	});
}


/* FExrMediaTextureSampleConverter implementation
 *****************************************************************************/

bool FExrMediaTextureSampleConverter::Convert(FTexture2DRHIRef& InDstTexture, const FConversionHints& Hints)
{
	return ConvertExrBufferCallback.Execute(FRHICommandListExecutor::GetImmediateCommandList(), InDstTexture);
}

#endif //IMGMEDIA_EXR_SUPPORTED_PLATFORM && PLATFORM_WINDOWS

