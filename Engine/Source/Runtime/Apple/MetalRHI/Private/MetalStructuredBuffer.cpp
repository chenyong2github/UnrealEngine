// Copyright Epic Games, Inc. All Rights Reserved.


#include "MetalRHIPrivate.h"
#include "MetalProfiler.h"
#include "MetalCommandBuffer.h"
#include "Containers/ResourceArray.h"

static uint32 MetalStructuredBufferUsage(uint32 InUsage)
{
	return (InUsage | EMetalBufferUsage_GPUOnly);
}

FStructuredBufferRHIRef FMetalDynamicRHI::RHICreateStructuredBuffer(uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	@autoreleasepool {
		return new FMetalResourceMultiBuffer(Size, MetalStructuredBufferUsage(InUsage), Stride, CreateInfo.ResourceArray, RRT_StructuredBuffer);
	}
}

void* FMetalDynamicRHI::LockStructuredBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	@autoreleasepool {
	FMetalStructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);
	
	// just return the memory plus the offset
	return (uint8*)StructuredBuffer->Lock(true, LockMode, Offset, Size);
	}
}

void FMetalDynamicRHI::UnlockStructuredBuffer_BottomOfPipe(FRHICommandListImmediate& RHICmdList, FRHIStructuredBuffer* StructuredBufferRHI)
{
	@autoreleasepool {
	FMetalStructuredBuffer* StructuredBuffer = ResourceCast(StructuredBufferRHI);
	StructuredBuffer->Unlock();
	}
}

FStructuredBufferRHIRef FMetalDynamicRHI::CreateStructuredBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	@autoreleasepool {
		// make the RHI object, which will allocate memory
		TRefCountPtr<FMetalResourceMultiBuffer> VertexBuffer = new FMetalResourceMultiBuffer(Size, MetalStructuredBufferUsage(InUsage), Stride, nullptr, RRT_StructuredBuffer);
		
		VertexBuffer->Init_RenderThread(RHICmdList, Size, InUsage, CreateInfo, VertexBuffer);
		
		return VertexBuffer.GetReference();
	}
}
