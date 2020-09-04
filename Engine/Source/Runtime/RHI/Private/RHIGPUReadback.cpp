// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RHIGPUReadback.cpp: Convenience function implementations for async GPU 
	memory updates and readbacks
=============================================================================*/

#include "RHIGPUReadback.h"

///////////////////////////////////////////////////////////////////////////////
//////////////////////     FGenericRHIGPUFence    /////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FGenericRHIGPUFence::FGenericRHIGPUFence(FName InName)
	: FRHIGPUFence(InName)
	, InsertedFrameNumber(MAX_uint32)
{}

void FGenericRHIGPUFence::Clear()
{
	InsertedFrameNumber = MAX_uint32;
}

void FGenericRHIGPUFence::WriteInternal()
{
	// GPU generally overlap the game. This overlap increases when using AFR. In normal mode this can make us appear to be further behind the gpu than we actually are.
	InsertedFrameNumber = GFrameNumberRenderThread + GNumAlternateFrameRenderingGroups;
}

bool FGenericRHIGPUFence::Poll() const
{
	const uint32 CurrentFrameNumber = GFrameNumberRenderThread;
	if (CurrentFrameNumber > InsertedFrameNumber)
	{
		return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////
////////////////////     FGenericRHIStagingBuffer    //////////////////////////
///////////////////////////////////////////////////////////////////////////////

void* FGenericRHIStagingBuffer::Lock(uint32 InOffset, uint32 NumBytes)
{
	check(ShadowBuffer);
	check(!bIsLocked);
	bIsLocked = true;
	return reinterpret_cast<void*>(reinterpret_cast<uint8*>(RHILockVertexBuffer(ShadowBuffer, InOffset, NumBytes, RLM_ReadOnly)) + Offset);
}

void FGenericRHIStagingBuffer::Unlock()
{
	check(bIsLocked);
	RHIUnlockVertexBuffer(ShadowBuffer);
	bIsLocked = false;
}

///////////////////////////////////////////////////////////////////////////////
////////////////////     FRHIGPUBufferReadback    /////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FRHIGPUBufferReadback::FRHIGPUBufferReadback(FName RequestName) : FRHIGPUMemoryReadback(RequestName)
{
	DestinationStagingBuffer = RHICreateStagingBuffer();
}

void FRHIGPUBufferReadback::EnqueueCopy(FRHICommandList& RHICmdList, FRHIVertexBuffer* SourceBuffer, uint32 NumBytes)
{
	Fence->Clear();
	RHICmdList.CopyToStagingBuffer(SourceBuffer, DestinationStagingBuffer, 0, NumBytes ? NumBytes : SourceBuffer->GetSize());
	RHICmdList.WriteGPUFence(Fence);
	LastCopyGPUMask = RHICmdList.GetGPUMask();
}

void* FRHIGPUBufferReadback::Lock(uint32 NumBytes)
{
	if (DestinationStagingBuffer)
	{
		ensure(Fence->Poll());
		return RHILockStagingBuffer(DestinationStagingBuffer, Fence.GetReference(), 0, NumBytes);
	}
	else
	{
		return nullptr;
	}
}

void FRHIGPUBufferReadback::Unlock()
{
	ensure(DestinationStagingBuffer);
	RHIUnlockStagingBuffer(DestinationStagingBuffer);
}

///////////////////////////////////////////////////////////////////////////////
////////////////////     FRHIGPUTextureReadback    ////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FRHIGPUTextureReadback::FRHIGPUTextureReadback(FName RequestName) : FRHIGPUMemoryReadback(RequestName)
{
}

void FRHIGPUTextureReadback::EnqueueCopyRDG(FRHICommandList& RHICmdList, FRHITexture* SourceTexture, FResolveRect Rect)
{
	FResolveParams ResolveParams(Rect);
	ResolveParams.SourceAccessFinal = ERHIAccess::Unknown;
	ResolveParams.DestAccessFinal = ERHIAccess::Unknown;
	EnqueueCopyInternal(RHICmdList, SourceTexture, ResolveParams);
}

void FRHIGPUTextureReadback::EnqueueCopy(FRHICommandList& RHICmdList, FRHITexture* SourceTexture, FResolveRect Rect)
{
	FResolveParams ResolveParams(Rect);
	ResolveParams.SourceAccessFinal = ERHIAccess::CopySrc;
	ResolveParams.DestAccessFinal = ERHIAccess::CopyDest;
	EnqueueCopyInternal(RHICmdList, SourceTexture, ResolveParams);
}

void FRHIGPUTextureReadback::EnqueueCopyInternal(FRHICommandList& RHICmdList, FRHITexture* SourceTexture, FResolveParams ResolveParams)
{
	Fence->Clear();

	if (SourceTexture)
	{
		// We only support 2d textures for now.
		ensure(SourceTexture->GetTexture2D());

		// Assume for now that every enqueue happens on a texture of the same format and size (when reused).
		if (!DestinationStagingBuffer)
		{
			FIntVector TextureSize = SourceTexture->GetSizeXYZ();

			FString FenceName = Fence->GetFName().ToString();
			FRHIResourceCreateInfo CreateInfo(*FenceName);
			DestinationStagingBuffer = RHICreateTexture2D(TextureSize.X, TextureSize.Y, SourceTexture->GetFormat(), 1, 1, TexCreate_CPUReadback | TexCreate_HideInVisualizeTexture, CreateInfo);
		}

		// Transfer memory GPU -> CPU
		RHICmdList.Transition(FRHITransitionInfo(DestinationStagingBuffer, ERHIAccess::Unknown, ERHIAccess::CopyDest));
		RHICmdList.CopyToResolveTarget(SourceTexture, DestinationStagingBuffer, ResolveParams);
		RHICmdList.WriteGPUFence(Fence);
		LastCopyGPUMask = RHICmdList.GetGPUMask();
	}
}

void* FRHIGPUTextureReadback::Lock(uint32 NumBytes)
{
	if (DestinationStagingBuffer)
	{
		void* ResultsBuffer = nullptr;
		int32 BufferWidth = 0, BufferHeight = 0;
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		RHICmdList.MapStagingSurface(DestinationStagingBuffer, Fence.GetReference(), ResultsBuffer, BufferWidth, BufferHeight);
		return ResultsBuffer;
	}
	else
	{
		return nullptr;
	}
}

void FRHIGPUTextureReadback::LockTexture(FRHICommandListImmediate& RHICmdList, void*& OutBufferPtr, int32& OutRowPitchInPixels)
{
	if (DestinationStagingBuffer)
	{
		void* ResultsBuffer = nullptr;
		int32 BufferWidth = 0, BufferHeight = 0;
		RHICmdList.MapStagingSurface(DestinationStagingBuffer, Fence.GetReference(), ResultsBuffer, BufferWidth, BufferHeight);
		OutBufferPtr = ResultsBuffer;
		OutRowPitchInPixels = BufferWidth;
	}
	else
	{
		OutBufferPtr = nullptr;
		OutRowPitchInPixels = 0;
	}
}

void FRHIGPUTextureReadback::Unlock()
{
	ensure(DestinationStagingBuffer);

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	RHICmdList.UnmapStagingSurface(DestinationStagingBuffer);
}
