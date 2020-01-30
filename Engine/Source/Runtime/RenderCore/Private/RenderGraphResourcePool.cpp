// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RenderTargetPool.cpp: Scene render target pool manager.
=============================================================================*/

#include "RenderGraphResourcePool.h"
#include "RenderGraphResources.h"

FRenderGraphResourcePool::FRenderGraphResourcePool()
{ }


void FRenderGraphResourcePool::FindFreeBuffer(
	FRHICommandList& RHICmdList,
	const FRDGBufferDesc& Desc,
	TRefCountPtr<FPooledRDGBuffer>& Out,
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
			Out = PooledBuffer;
			Out->LastUsedFrame = FrameCounter;
			Out->Name = InDebugName;
			// TODO(RDG): assign name on RHI.
			return;
		}
	}

	// Allocate new one
	{
		Out = new FPooledRDGBuffer;
		AllocatedBuffers.Add(Out);
		check(Out->GetRefCount() == 2);

		Out->Desc = Desc;
		Out->Name = InDebugName;
		Out->LastUsedFrame = FrameCounter;

		uint32 NumBytes = Desc.GetTotalNumBytes();

		FRHIResourceCreateInfo CreateInfo;
		CreateInfo.DebugName = InDebugName;

		if (Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer)
		{
			Out->VertexBuffer = RHICreateVertexBuffer(NumBytes, Desc.Usage, CreateInfo);
		}
		else if (Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
		{
			Out->StructuredBuffer = RHICreateStructuredBuffer(Desc.BytesPerElement, NumBytes, Desc.Usage, CreateInfo);
		}
		else
		{
			check(0);
		}
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
		TRefCountPtr<FPooledRDGBuffer>& Buffer = AllocatedBuffers[BufferIndex];

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
