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
			PooledBuffer->Name = InDebugName;
			
			//@todo - rename other resources too
			for (const auto& Pair : PooledBuffer->UAVs)
			{
				RHIBindDebugLabelName(Pair.Value, InDebugName);
			}

			return PooledBuffer;
		}
	}

	// Allocate new one
	{
		TRefCountPtr<FRDGPooledBuffer> PooledBuffer = new FRDGPooledBuffer(Desc);
		AllocatedBuffers.Add(PooledBuffer);
		check(PooledBuffer->GetRefCount() == 2);

		PooledBuffer->Name = InDebugName;
		PooledBuffer->LastUsedFrame = FrameCounter;

		uint32 NumBytes = Desc.GetTotalNumBytes();

		FRHIResourceCreateInfo CreateInfo;
		CreateInfo.DebugName = InDebugName;

		if (Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer)
		{
			PooledBuffer->VertexBuffer = RHICreateVertexBuffer(NumBytes, Desc.Usage, CreateInfo);
		}
		else if (Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
		{
			PooledBuffer->StructuredBuffer = RHICreateStructuredBuffer(Desc.BytesPerElement, NumBytes, Desc.Usage, CreateInfo);
		}
		else
		{
			check(0);
		}

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
