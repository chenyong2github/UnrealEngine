// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalIndexBuffer.cpp: Metal Index buffer RHI implementation.
=============================================================================*/

#include "MetalRHIPrivate.h"
#include "MetalProfiler.h"
#include "MetalCommandBuffer.h"
#include "MetalCommandQueue.h"
#include "Containers/ResourceArray.h"
#include "RenderUtils.h"
#include "HAL/LowLevelMemTracker.h"

static uint32 MetalIndexBufferUsage(uint32 InUsage)
{
	uint32 Usage = InUsage | BUF_IndexBuffer;
	if (RHISupportsTessellation(GMaxRHIShaderPlatform))
	{
		Usage |= BUF_ShaderResource;
	}
	Usage |= (EMetalBufferUsage_GPUOnly | EMetalBufferUsage_LinearTex);
	return Usage;
}

FIndexBufferRHIRef FMetalDynamicRHI::RHICreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	@autoreleasepool {
	if (CreateInfo.bWithoutNativeResource)
	{
		return new FMetalResourceMultiBuffer(0, MetalIndexBufferUsage(0), 2, nullptr, RRT_IndexBuffer);
	}
		
	// make the RHI object, which will allocate memory
	FMetalResourceMultiBuffer* IndexBuffer = new FMetalResourceMultiBuffer(Size, MetalIndexBufferUsage(InUsage), Stride, nullptr, RRT_IndexBuffer);
	
	if (CreateInfo.ResourceArray)
	{
		check(Size == CreateInfo.ResourceArray->GetResourceDataSize());

		// make a buffer usable by CPU
		void* Buffer = ::RHILockIndexBuffer(IndexBuffer, 0, Size, RLM_WriteOnly);

		// copy the contents of the given data into the buffer
		FMemory::Memcpy(Buffer, CreateInfo.ResourceArray->GetResourceData(), Size);

		::RHIUnlockIndexBuffer(IndexBuffer);

		// Discard the resource array's contents.
		CreateInfo.ResourceArray->Discard();
	}

	return IndexBuffer;
	}
}

void FMetalDynamicRHI::RHITransferBufferUnderlyingResource(FRHIBuffer* DestBuffer, FRHIBuffer* SrcBuffer)
{
	@autoreleasepool {
	check(DestBuffer);
	FMetalResourceMultiBuffer* Dest = ResourceCast(DestBuffer);
	if (!SrcBuffer)
	{
		TRefCountPtr<FMetalResourceMultiBuffer> DeletionProxy = new FMetalResourceMultiBuffer(0, Dest->GetUsage(), Dest->GetStride(), nullptr, Dest->Type);
		Dest->Swap(*DeletionProxy);
	}
	else
	{
		FMetalResourceMultiBuffer* Src = ResourceCast(SrcBuffer);
		Dest->Swap(*Src);
	}
	}
}

void* FMetalDynamicRHI::LockIndexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	@autoreleasepool {
	FMetalIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);

	return (uint8*)IndexBuffer->Lock(true, LockMode, Offset, Size);
	}
}

void FMetalDynamicRHI::UnlockIndexBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBufferRHI)
{
	@autoreleasepool {
	FMetalIndexBuffer* IndexBuffer = ResourceCast(IndexBufferRHI);

	IndexBuffer->Unlock();
	}
}

FIndexBufferRHIRef FMetalDynamicRHI::CreateIndexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	@autoreleasepool {
		if (CreateInfo.bWithoutNativeResource)
		{
			return new FMetalResourceMultiBuffer(0, MetalIndexBufferUsage(0), 2, nullptr, RRT_IndexBuffer);
		}
		
		// make the RHI object, which will allocate memory
		TRefCountPtr<FMetalResourceMultiBuffer> IndexBuffer = new FMetalResourceMultiBuffer(Size, MetalIndexBufferUsage(InUsage), Stride, nullptr, RRT_IndexBuffer);
		
		IndexBuffer->Init_RenderThread(RHICmdList, Size, InUsage, CreateInfo, IndexBuffer);
		
		return IndexBuffer.GetReference();
	}
}
