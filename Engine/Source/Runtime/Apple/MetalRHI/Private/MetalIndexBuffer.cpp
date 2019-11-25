// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	uint32 Usage = InUsage;
	if (RHISupportsTessellation(GMaxRHIShaderPlatform))
	{
		Usage |= BUF_ShaderResource;
	}
	Usage |= EMetalBufferUsage_GPUOnly|EMetalBufferUsage_LinearTex;
	return Usage;
}

/** Constructor */
FMetalIndexBuffer::FMetalIndexBuffer(uint32 InStride, uint32 InSize, uint32 InUsage)
	: FRHIIndexBuffer(InStride, InSize, InUsage)
	, FMetalRHIBuffer(InSize, MetalIndexBufferUsage(InUsage), RRT_IndexBuffer)
	, IndexType((InStride == 2) ? mtlpp::IndexType::UInt16 : mtlpp::IndexType::UInt32)
{
	if (RHISupportsTessellation(GMaxRHIShaderPlatform))
	{
		EPixelFormat Format = IndexType == mtlpp::IndexType::UInt16 ? PF_R16_UINT : PF_R32_UINT;
		CreateLinearTexture(Format, this);
	}
}

FMetalIndexBuffer::~FMetalIndexBuffer()
{
}

void FMetalIndexBuffer::Swap(FMetalIndexBuffer& Other)
{
	@autoreleasepool {
	FRHIIndexBuffer::Swap(Other);
	FMetalRHIBuffer::Swap(Other);
	::Swap(IndexType, Other.IndexType);
	}
}

FIndexBufferRHIRef FMetalDynamicRHI::RHICreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	@autoreleasepool {
	if (CreateInfo.bWithoutNativeResource)
	{
		return new FMetalIndexBuffer(2, 0, 0);
	}
		
	// make the RHI object, which will allocate memory
	FMetalIndexBuffer* IndexBuffer = new FMetalIndexBuffer(Stride, Size, InUsage);
	
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
	else if (IndexBuffer->Mode == mtlpp::StorageMode::Private)
	{
		check (!IndexBuffer->CPUBuffer);

		if (GMetalBufferZeroFill && !FMetalCommandQueue::SupportsFeature(EMetalFeaturesFences))
		{
			GetMetalDeviceContext().FillBuffer(IndexBuffer->Buffer, ns::Range(0, IndexBuffer->Buffer.GetLength()), 0);
		}
	}
#if PLATFORM_MAC
	else if (GMetalBufferZeroFill && IndexBuffer->Mode == mtlpp::StorageMode::Managed)
	{
		MTLPP_VALIDATE(mtlpp::Buffer, IndexBuffer->Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, DidModify(ns::Range(0, IndexBuffer->Buffer.GetLength())));
	}
#endif

	return IndexBuffer;
	}
}

void FMetalDynamicRHI::RHITransferIndexBufferUnderlyingResource(FRHIIndexBuffer* DestIndexBuffer, FRHIIndexBuffer* SrcIndexBuffer)
{
	@autoreleasepool {
	check(DestIndexBuffer);
	FMetalIndexBuffer* Dest = ResourceCast(DestIndexBuffer);
	if (!SrcIndexBuffer)
	{
		FRHIResourceCreateInfo CreateInfo;
		TRefCountPtr<FMetalIndexBuffer> DeletionProxy = new FMetalIndexBuffer(2, 0, 0);
		Dest->Swap(*DeletionProxy);
	}
	else
	{
		FMetalIndexBuffer* Src = ResourceCast(SrcIndexBuffer);
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

FIndexBufferRHIRef FMetalDynamicRHI::CreateIndexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	@autoreleasepool {
		if (CreateInfo.bWithoutNativeResource)
		{
			return new FMetalIndexBuffer(2, 0, 0);
		}
		
		// make the RHI object, which will allocate memory
		TRefCountPtr<FMetalIndexBuffer> IndexBuffer = new FMetalIndexBuffer(Stride, Size, InUsage);
		
		IndexBuffer->Init_RenderThread(RHICmdList, Size, InUsage, CreateInfo, IndexBuffer);
		
		return IndexBuffer.GetReference();
	}
}
