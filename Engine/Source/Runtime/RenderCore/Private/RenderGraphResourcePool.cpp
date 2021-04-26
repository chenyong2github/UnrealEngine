// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RenderTargetPool.cpp: Scene render target pool manager.
=============================================================================*/

#include "RenderGraphResourcePool.h"
#include "RenderGraphResources.h"

FRenderGraphResourcePool::FRenderGraphResourcePool()
{ }


TRefCountPtr<FRDGPooledBuffer> FRenderGraphResourcePool::FindFreeBuffer(
	FRHICommandList& RHICmdList,
	const FRDGBufferDesc& Desc,
	const TCHAR* InDebugName)
{
	TRefCountPtr<FRDGPooledBuffer> Result = FindFreeBufferInternal(RHICmdList, Desc, InDebugName);
	Result->Reset();
	return Result;
}

TRefCountPtr<FRDGPooledBuffer> FRenderGraphResourcePool::FindFreeBufferInternal(
	FRHICommandList& RHICmdList,
	const FRDGBufferDesc& Desc,
	const TCHAR* InDebugName)
{
	// First find if available.
	for (auto& PooledBuffer : AllocatedBuffers)
	{
		// Still being used outside the pool.
		if (PooledBuffer->GetRefCount() > 1)
		{
			continue;
		}

		if (PooledBuffer->Desc == Desc)
		{
			PooledBuffer->LastUsedFrame = FrameCounter;
			PooledBuffer->ViewCache.SetDebugName(InDebugName);
			PooledBuffer->Name = InDebugName;

			return PooledBuffer;
		}
	}

	// Allocate new one
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FRenderGraphResourcePool::CreateBuffer);

		uint32 NumBytes = Desc.GetTotalNumBytes();

		FRHIResourceCreateInfo CreateInfo(InDebugName);
		TRefCountPtr<FRHIBuffer> BufferRHI;

		if (Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer)
		{
			BufferRHI = RHICreateVertexBuffer(NumBytes, Desc.Usage, CreateInfo);
		}
		else if (Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
		{
			BufferRHI = RHICreateStructuredBuffer(Desc.BytesPerElement, NumBytes, Desc.Usage, CreateInfo);
		}
		else if (Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::AccelerationStructure)
		{
			BufferRHI = RHICreateBuffer(NumBytes, Desc.Usage, 0, ERHIAccess::BVHWrite, CreateInfo);
		}
		else
		{
			check(0);
		}

		TRefCountPtr<FRDGPooledBuffer> PooledBuffer = new FRDGPooledBuffer(MoveTemp(BufferRHI), Desc);
		AllocatedBuffers.Add(PooledBuffer);
		check(PooledBuffer->GetRefCount() == 2);

		PooledBuffer->Name = InDebugName;
		PooledBuffer->LastUsedFrame = FrameCounter;

		return PooledBuffer;
	}
}

void FRenderGraphResourcePool::ReleaseDynamicRHI()
{
	AllocatedBuffers.Empty();
}

void FRenderGraphResourcePool::TickPoolElements()
{
	const uint32 kFramesUntilRelease = 30;

	int32 BufferIndex = 0;

	while (BufferIndex < AllocatedBuffers.Num())
	{
		TRefCountPtr<FRDGPooledBuffer>& Buffer = AllocatedBuffers[BufferIndex];

		const bool bIsUnused = Buffer.GetRefCount() == 1;

		const bool bNotRequestedRecently = (FrameCounter - Buffer->LastUsedFrame) > kFramesUntilRelease;

		if (bIsUnused && bNotRequestedRecently)
		{
			Swap(Buffer, AllocatedBuffers.Last());
			AllocatedBuffers.Pop();
		}
		else
		{
			++BufferIndex;
		}
	}

	++FrameCounter;
}

TGlobalResource<FRenderGraphResourcePool> GRenderGraphResourcePool;
