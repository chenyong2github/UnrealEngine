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

class CustomFence : public FD3D12GPUFence
{
public:
	void ResetValue() { Value = 0; }
};

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

bool FExrImgMediaReaderGpu::ReadFrame(const FString& ImagePath, TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> OutFrame, int32 FrameId)
{
	FRgbaInputFile InputFile(ImagePath);

	if (!GetInfo(InputFile, OutFrame->Info))
	{
		return false;
	}

	const FIntPoint& Dim = OutFrame->Info.Dim;

	if (Dim.GetMin() <= 0)
	{
		return false;
	}

	const int32 NumChannels = OutFrame->Info.NumChannels;
	const int32 PixelSize = sizeof(uint16) * NumChannels;

	// At the beginning of each row of B G R channel planes there is 2x4 byte data that has information
	// about number of pixels in the current row and row's number.
	const uint16 PlanePadding = 8;
	const SIZE_T BufferSize = Dim.X * Dim.Y * sizeof(uint16) * NumChannels + Dim.Y * PlanePadding;
	FStructuredBufferPoolItemSharedPtr BufferData;
	{
		BufferData = AllocateGpuBufferFromPool(BufferSize);
		bool bResult = false;

#if READ_IN_CHUNKS
		bResult = ReadInChunks(static_cast<uint16*>(BufferData->MappedBuffer), ImagePath, FrameId, Dim, BufferSize, PixelSize, NumChannels);
#else
		bResult = FExrReader::GenerateTextureData(Buffer, ImagePath, Dim.X, Dim.Y, PixelSize, NumChannels);
#endif

		if (!bResult)
		{
			return false;
		}
	}

	OutFrame->Format = NumChannels == 3 ? EMediaTextureSampleFormat::FloatRGB : EMediaTextureSampleFormat::FloatRGBA;
	OutFrame->Stride = Dim.X * PixelSize;
	auto RenderThreadSwizzler = [this, BufferSize, BufferData, Dim, FrameId, NumChannels](FRHICommandListImmediate& RHICmdList, FTexture2DRHIRef RenderTargetTextureRHI)->bool
	{
		if (!BufferData->BufferRef->IsValid())
		{
			return false;
		}
		
		// This flag will indicate that we should wait for poll to complete.
		BufferData->bWillBeSignaled = true;

		FRHIRenderPassInfo RPInfo(RenderTargetTextureRHI, ERenderTargetActions::DontLoad_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("ExrTextureSwizzle"));

		FExrSwizzlePS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FExrSwizzlePS::FRgbaSwizzle>(NumChannels > 3);
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
		static_cast<FD3D12GPUFence*>(BufferData->Fence.GetReference())->WriteInternal(ED3D12CommandQueueType::Default);

		//Doesn't need further conversion so returning false.
		return false;
	};

	FExrMediaTextureSampleConverter* SampleConverter = new FExrMediaTextureSampleConverter();
	SampleConverter->ConvertExrBufferCallback = FExrConvertBufferCallback::CreateLambda(RenderThreadSwizzler);
	OutFrame->SampleConverter = MakeShareable(SampleConverter);
	UE_LOG(LogImgMedia, Log, TEXT("Reader %p: Read Pixels Complete. %i"), this, FrameId);

	return true;
}

void FExrImgMediaReaderGpu::PreAllocateMemoryPool(int32 NumFrames, int32 AllocSize)
{
	for (int32 FrameCacheNum = 0; FrameCacheNum < NumFrames; FrameCacheNum++)
	{
		AllocateGpuBufferFromPool(AllocSize, FrameCacheNum == NumFrames - 1);
	}
}


void FExrImgMediaReaderGpu::OnTick()
{
	TransferFromStagingBuffer();
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
			ENQUEUE_RENDER_COMMAND(CreatePooledBuffer)([AllocatedBuffer, AllocSize, &bInitDone, this](FRHICommandListImmediate& RHICmdList)
			{
				FScopeLock ScopeLock(&AllocatorCriticalSecion);
				FRHIResourceCreateInfo CreateInfo;
				AllocatedBuffer->BufferRef = RHICreateStructuredBuffer(sizeof(uint16) * 2., AllocSize, BUF_ShaderResource | BUF_Dynamic | BUF_FastVRAM | BUF_Transient, CreateInfo);
				AllocatedBuffer->MappedBuffer = static_cast<uint16*>(RHILockStructuredBuffer(AllocatedBuffer->BufferRef, 0, AllocSize, RLM_WriteOnly));
				AllocatedBuffer->Fence = RHICreateGPUFence(TEXT("BufferNoLongerInUseFence"));
				bInitDone = true;
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
			FScopeLock ScopeLock(&AllocatorCriticalSecion);
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

