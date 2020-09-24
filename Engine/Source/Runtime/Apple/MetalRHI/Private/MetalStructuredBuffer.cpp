// Copyright Epic Games, Inc. All Rights Reserved.


#include "MetalRHIPrivate.h"
#include "MetalProfiler.h"
#include "MetalCommandBuffer.h"
#include "Containers/ResourceArray.h"

FMetalStructuredBuffer::FMetalStructuredBuffer(uint32 Stride, uint32 InSize, FResourceArrayInterface* ResourceArray, uint32 InUsage)
	: FRHIStructuredBuffer(Stride, InSize, InUsage)
	, FMetalRHIBuffer(InSize, InUsage|EMetalBufferUsage_GPUOnly, RRT_StructuredBuffer)
{
	check((InSize % Stride) == 0);
	
	if (ResourceArray)
	{
		// copy any resources to the CPU address
		void* LockedMemory = RHILockStructuredBuffer(this, 0, InSize, RLM_WriteOnly);
 		FMemory::Memcpy(LockedMemory, ResourceArray->GetResourceData(), InSize);
		ResourceArray->Discard();
		RHIUnlockStructuredBuffer(this);
	}
}

FMetalStructuredBuffer::~FMetalStructuredBuffer()
{
}


FStructuredBufferRHIRef FMetalDynamicRHI::RHICreateStructuredBuffer(uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	@autoreleasepool {
		FMetalStructuredBuffer* Buffer = new FMetalStructuredBuffer(Stride, Size, CreateInfo.ResourceArray, InUsage);
//		if (!CreateInfo.ResourceArray && Buffer->Mode == mtlpp::StorageMode::Private)
//		{
//			if (Buffer->TransferBuffer)
//			{
//				SafeReleaseMetalBuffer(Buffer->TransferBuffer);
//				Buffer->TransferBuffer = nil;
//			}
//		}
		return Buffer;
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
		TRefCountPtr<FMetalStructuredBuffer> VertexBuffer = new FMetalStructuredBuffer(Stride, Size, nullptr, InUsage);
		
		VertexBuffer->Init_RenderThread(RHICmdList, Size, InUsage, CreateInfo, VertexBuffer);
		
		return VertexBuffer.GetReference();
	}
}
