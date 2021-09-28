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

#include "D3D12RHIPrivate.h"
#include "D3D12Util.h"
#include "D3D12State.h"
#include "D3D12Resources.h"
#include "D3D12RHICommon.h"
#include "D3D12Viewport.h"
#include "D3D12ConstantBuffer.h"
#include "D3D12DirectCommandListManager.h"

#define READ_IN_CHUNKS 1
#define EXR_ENABLE_MIPS 1

DECLARE_GPU_STAT_NAMED(ExrImgMediaReaderGpu, TEXT("ExrImgMediaReaderGpu"));

namespace {
	/** This function is similar to DrawScreenPass in OpenColorIODisplayExtension.cpp except it is catered for Viewless texture rendering. */
	template<typename TSetupFunction>
	void DrawScreenPass(
		FRHICommandListImmediate& RHICmdList,
		const FIntPoint& OutputResolution,
		const FScreenPassPipelineState& PipelineState,
		TSetupFunction SetupFunction)
	{
		RHICmdList.SetViewport(0.f, 0.f, 0.f, OutputResolution.X, OutputResolution.Y, 1.0f);

		SetScreenPassPipelineState(RHICmdList, PipelineState);

		// Setting up buffers.
		SetupFunction(RHICmdList);

		FIntPoint LocalOutputPos(FIntPoint::ZeroValue);
		FIntPoint LocalOutputSize(OutputResolution);
		EDrawRectangleFlags DrawRectangleFlags = EDRF_UseTriangleOptimization;

		DrawPostProcessPass(
			RHICmdList,
			LocalOutputPos.X, LocalOutputPos.Y, LocalOutputSize.X, LocalOutputSize.Y,
			0., 0., OutputResolution.X, OutputResolution.Y,
			OutputResolution,
			OutputResolution,
			PipelineState.VertexShader,
			EStereoscopicPass::eSSP_FULL,
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
					RHIUnlockStructuredBuffer(MemoryPoolItem->BufferRef);
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

bool FExrImgMediaReaderGpu::ReadFrame(int32 FrameId, int32 MipLevel, const FImgMediaTileSelection& InTileSelection, TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> OutFrame)
{
	TSharedPtr<FImgMediaLoader, ESPMode::ThreadSafe> Loader = LoaderPtr.Pin();
	if (Loader.IsValid() == false)
	{
		return false;
	}

	const FString& LargestImagePath = Loader->GetImagePath(FrameId, 0);
	FRgbaInputFile InputFile(LargestImagePath);

	if (!GetInfo(InputFile, OutFrame->Info))
	{
		return false;
	}

	const FIntPoint& LargestDim = OutFrame->Info.Dim;

	if (LargestDim.GetMin() <= 0)
	{
		return false;
	}

	const int32 NumChannels = OutFrame->Info.NumChannels;
	const int32 PixelSize = sizeof(uint16) * NumChannels;

	FStructuredBufferPoolItemSharedPtr BufferDataArray[FImgMediaLoader::MAX_MIPMAP_LEVELS];

	int32 NumMipLevels = Loader->GetNumMipLevels();
	{
		// Loop over all mips.
		FIntPoint Dim = LargestDim;
		for (int32 CurrentMipLevel = 0; CurrentMipLevel < NumMipLevels; ++CurrentMipLevel)
		{
			// Do we want to read in this mip?
			bool IsThisLevelPresent = (OutFrame->MipMapsPresent & (1 << CurrentMipLevel)) != 0;
			bool ReadThisMip = (CurrentMipLevel >= MipLevel) &&
				(IsThisLevelPresent == false);

#if EXR_ENABLE_MIPS == 0
			// Just read in mip 0 if we don't already have it.
			if ((CurrentMipLevel > 0) || (OutFrame->MipMapsPresent != 0))
			{
				break;
			}
			ReadThisMip = true;
#endif // EXR_ENABLE_MIPS == 0

			if (ReadThisMip)
			{
				// Get for our frame/mip level.
				const FString& ImagePath = Loader->GetImagePath(FrameId, CurrentMipLevel);
				bool bResult = false;
				
				const SIZE_T BufferSize = GetBufferSize(Dim, NumChannels);
				FStructuredBufferPoolItemSharedPtr& BufferData = BufferDataArray[CurrentMipLevel];
				BufferData = AllocateGpuBufferFromPool(BufferSize);
				uint16* MipDataPtr = static_cast<uint16*>(BufferData->MappedBuffer);

#if READ_IN_CHUNKS
				bResult = ReadInChunks(MipDataPtr, ImagePath, FrameId, Dim, BufferSize, PixelSize, NumChannels);
#else
				bResult = FExrReader::GenerateTextureData(MipDataPtr, ImagePath, Dim.X, Dim.Y, PixelSize, NumChannels);
#endif

				if (!bResult)
				{
					return false;
				}
			}

			// Next level.
			Dim /= 2;
		}
	}

	OutFrame->Format = NumChannels <= 3 ? EMediaTextureSampleFormat::FloatRGB : EMediaTextureSampleFormat::FloatRGBA;
	OutFrame->Stride = LargestDim.X * PixelSize;
	auto RenderThreadSwizzler = [this, BufferDataArray, LargestDim, FrameId, NumChannels, NumMipLevels](FRHICommandListImmediate& RHICmdList, FTexture2DRHIRef RenderTargetTextureRHI)->bool
	{
		SCOPED_DRAW_EVENT(RHICmdList, FExrImgMediaReaderGpu_Convert);
		SCOPED_GPU_STAT(RHICmdList, ExrImgMediaReaderGpu);

		FIntPoint Dim = LargestDim;
		for (int32 MipLevel = 0; MipLevel < NumMipLevels; ++MipLevel)
		{
			FStructuredBufferPoolItemSharedPtr BufferData = BufferDataArray[MipLevel];
			if (BufferData.IsValid())
			{
				if (!BufferData->BufferRef->IsValid())
				{
					continue;
				}
				// This flag will indicate that we should wait for poll to complete.
				BufferData->bWillBeSignaled = true;

				FRHIRenderPassInfo RPInfo(RenderTargetTextureRHI, ERenderTargetActions::DontLoad_Store, nullptr, MipLevel);
				RHICmdList.BeginRenderPass(RPInfo, TEXT("ExrTextureSwizzle"));

				FExrSwizzlePS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FExrSwizzlePS::FRgbaSwizzle>(NumChannels);
				FExrSwizzlePS::FParameters Parameters = FExrSwizzlePS::FParameters();
				Parameters.TextureWidth = Dim.X;
				Parameters.TextureHeight = Dim.Y;

				Parameters.UnswizzledBuffer = RHICreateShaderResourceView(BufferData->BufferRef);

				FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

				TShaderMapRef<FExrSwizzleVS> SwizzleShaderVS(ShaderMap);
				TShaderMapRef<FExrSwizzlePS> SwizzleShaderPS(ShaderMap, PermutationVector);

				FScreenPassPipelineState PipelineState(SwizzleShaderVS, SwizzleShaderPS, TStaticBlendState<>::GetRHI(), TStaticDepthStencilState<false, CF_Always>::GetRHI());
				DrawScreenPass(RHICmdList, Dim, PipelineState, [&](FRHICommandListImmediate& RHICmdList)
				{
					SetShaderParameters(RHICmdList, SwizzleShaderPS, SwizzleShaderPS.GetPixelShader(), Parameters);
				});

				// Resolve render target.
				RHICmdList.EndRenderPass();

				// Mark this render command for this buffer as complete, so we can poll it and transfer later.
				RHICmdList.WriteGPUFence(BufferData->Fence);
			}

			// Next level.
			Dim /= 2;
		}

		//Doesn't need further conversion so returning false.
		return false;
	};

	FExrMediaTextureSampleConverter* SampleConverter = new FExrMediaTextureSampleConverter();
	SampleConverter->ConvertExrBufferCallback = FExrConvertBufferCallback::CreateLambda(RenderThreadSwizzler);
	OutFrame->SampleConverter = MakeShareable(SampleConverter);
	OutFrame->MipMapsPresent = 1 << MipLevel;
	UE_LOG(LogImgMedia, Verbose, TEXT("Reader %p: Read Pixels Complete. %i"), this, FrameId);

	return true;
}

void FExrImgMediaReaderGpu::PreAllocateMemoryPool(int32 NumFrames, const FImgMediaFrameInfo& FrameInfo)
{
	SIZE_T AllocSize = GetBufferSize(FrameInfo.Dim, FrameInfo.NumChannels);
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

bool FExrImgMediaReaderGpu::ReadInChunks(uint16* Buffer, const FString& ImagePath, int32 FrameId, const FIntPoint& Dim, int32 BufferSize, int32 PixelSize, int32 NumChannels)
{
	bool bResult = true;

	// Chunks are of 16 MB
	const int32 ChunkSize = 0xF42400;
	const int32 Remainder = BufferSize % ChunkSize;
	const int32 NumChunks = (BufferSize - Remainder) / ChunkSize;
	int32 CurrentBufferPos = 0;
	
	FExrReader ChunkReader;


	if (!ChunkReader.OpenExrAndPrepareForPixelReading(ImagePath, Dim.X, Dim.Y, PixelSize, NumChannels))
	{
		return false;
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
				bResult = false;
				break;
			}
		}

		if (!ChunkReader.ReadExrImageChunk(reinterpret_cast<char*>(Buffer) + CurrentBufferPos, Step))
		{
			bResult = false;
			break;
		}
		CurrentBufferPos += Step;
	}

	if (!ChunkReader.CloseExrFile())
	{
		return false;
	}

	return bResult;
}

SIZE_T FExrImgMediaReaderGpu::GetBufferSize(const FIntPoint& Dim, int32 NumChannels)
{
	// At the beginning of each row of B G R channel planes there is 2x4 byte data that has information
	// about number of pixels in the current row and row's number.
	const uint16 PlanePadding = 8;

	SIZE_T BufferSize = Dim.X * Dim.Y * sizeof(uint16) * NumChannels + Dim.Y * PlanePadding;
	return BufferSize;
}

FStructuredBufferPoolItemSharedPtr FExrImgMediaReaderGpu::AllocateGpuBufferFromPool(uint32 AllocSize, bool bWait)
{
	// This function is attached to the shared pointer and is used to return any allocated memory to staging pool.
	auto BufferDeleter = [AllocSize, this](FStructuredBufferPoolItem* ObjectToDelete) {
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

			// Allocate and unlock the structured buffer on render thread.
			ENQUEUE_RENDER_COMMAND(CreatePooledBuffer)([AllocatedBuffer, AllocSize, &bInitDone, this, bWait](FRHICommandListImmediate& RHICmdList)
			{
				FScopeLock ScopeLock(&AllocatorCriticalSecion);
				SCOPED_DRAW_EVENT(RHICmdList, FExrImgMediaReaderGpu_AllocateBuffer);
				FRHIResourceCreateInfo CreateInfo;
				AllocatedBuffer->BufferRef = RHICreateStructuredBuffer(sizeof(uint16) * 2., AllocSize, BUF_ShaderResource | BUF_Dynamic | BUF_FastVRAM | BUF_Transient, CreateInfo);
				AllocatedBuffer->MappedBuffer = static_cast<uint16*>(RHILockStructuredBuffer(AllocatedBuffer->BufferRef, 0, AllocSize, RLM_WriteOnly));
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
	// If reader is being deleted, we don't need to return the memory into staging buffer and instead should delete it.
	if (bIsShuttingDown)
	{
		ENQUEUE_RENDER_COMMAND(DeletePooledBuffers)([this, Buffer](FRHICommandListImmediate& RHICmdList)
		{
			SCOPED_DRAW_EVENT(RHICmdList, FExrImgMediaReaderGpu_ReleaseBuffer);

			// By this point we don't need a lock because the destructor was already called and it 
			// is guaranteed that this buffer is no longer used anywhere else.
			RHIUnlockStructuredBuffer(Buffer->BufferRef);
			delete Buffer;
		});
	}
	else
	{
		FScopeLock ScopeLock(&AllocatorCriticalSecion);

		// We don't need to process this pooled buffer if the Reader is being destroyed.
		StagingMemoryPool.Add(AllocSize, Buffer);
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

