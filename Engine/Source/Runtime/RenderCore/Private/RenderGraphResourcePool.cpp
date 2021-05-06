// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RenderTargetPool.cpp: Scene render target pool manager.
=============================================================================*/

#include "RenderGraphResourcePool.h"
#include "RenderGraphResources.h"

uint64 ComputeHash(const FRDGBufferDesc& Desc)
{
	return CityHash64((const char*)&Desc, sizeof(FRDGBufferDesc));
}

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
	const uint64 BufferPageSize = 64 * 1024;

	FRDGBufferDesc AlignedDesc = Desc;
	AlignedDesc.NumElements = Align(AlignedDesc.BytesPerElement * AlignedDesc.NumElements, BufferPageSize) / AlignedDesc.BytesPerElement;
	const uint64 BufferHash = ComputeHash(AlignedDesc);

	// First find if available.
	for (int32 Index = 0; Index < AllocatedBufferHashes.Num(); ++Index)
	{
		if (AllocatedBufferHashes[Index] != BufferHash)
		{
			continue;
		}

		const auto& PooledBuffer = AllocatedBuffers[Index];

		// Still being used outside the pool.
		if (PooledBuffer->GetRefCount() > 1)
		{
			continue;
		}

		check(PooledBuffer->GetAlignedDesc() == AlignedDesc);

		PooledBuffer->LastUsedFrame = FrameCounter;
		PooledBuffer->ViewCache.SetDebugName(InDebugName);
		PooledBuffer->Name = InDebugName;

		// We need the external-facing desc to match what the user requested.
		const_cast<FRDGBufferDesc&>(PooledBuffer->Desc).NumElements = Desc.NumElements;

		return PooledBuffer;
	}

	// Allocate new one
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FRenderGraphResourcePool::CreateBuffer);

		const uint32 NumBytes = AlignedDesc.GetTotalNumBytes();

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

		TRefCountPtr<FRDGPooledBuffer> PooledBuffer = new FRDGPooledBuffer(MoveTemp(BufferRHI), Desc, AlignedDesc.NumElements, InDebugName);
		AllocatedBuffers.Add(PooledBuffer);
		AllocatedBufferHashes.Add(BufferHash);
		check(PooledBuffer->GetRefCount() == 2);

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
			AllocatedBuffers.RemoveAtSwap(BufferIndex);
			AllocatedBufferHashes.RemoveAtSwap(BufferIndex);
		}
		else
		{
			++BufferIndex;
		}
	}

	++FrameCounter;
}

TGlobalResource<FRenderGraphResourcePool> GRenderGraphResourcePool;
