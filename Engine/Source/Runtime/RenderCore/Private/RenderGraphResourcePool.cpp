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

TRefCountPtr<FRDGPooledBuffer> FRDGBufferPool::FindFreeBuffer(
	FRHICommandList& RHICmdList,
	const FRDGBufferDesc& Desc,
	const TCHAR* InDebugName)
{
	TRefCountPtr<FRDGPooledBuffer> Result = FindFreeBufferInternal(RHICmdList, Desc, InDebugName);
	Result->Reset();
	return Result;
}

TRefCountPtr<FRDGPooledBuffer> FRDGBufferPool::FindFreeBufferInternal(
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

	#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		RHIBindDebugLabelName(PooledBuffer->GetRHI(), InDebugName);
	#endif

		// We need the external-facing desc to match what the user requested.
		const_cast<FRDGBufferDesc&>(PooledBuffer->Desc).NumElements = Desc.NumElements;

		return PooledBuffer;
	}

	// Allocate new one
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FRDGBufferPool::CreateBuffer);

		const uint32 NumBytes = AlignedDesc.GetTotalNumBytes();

		FRHIResourceCreateInfo CreateInfo(InDebugName);
		TRefCountPtr<FRHIBuffer> BufferRHI;

		ERHIAccess InitialAccess = ERHIAccess::Unknown;

		if (Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::VertexBuffer)
		{
			const EBufferUsageFlags Usage = Desc.Usage | BUF_VertexBuffer;
			InitialAccess = RHIGetDefaultResourceState(Usage, false);
			BufferRHI = RHICreateVertexBuffer(NumBytes, Usage, InitialAccess, CreateInfo);
		}
		else if (Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::StructuredBuffer)
		{
			const EBufferUsageFlags Usage = Desc.Usage | BUF_StructuredBuffer;
			InitialAccess = RHIGetDefaultResourceState(Usage, false);
			BufferRHI = RHICreateStructuredBuffer(Desc.BytesPerElement, NumBytes, Usage, InitialAccess, CreateInfo);
		}
		else if (Desc.UnderlyingType == FRDGBufferDesc::EUnderlyingType::AccelerationStructure)
		{
			InitialAccess = ERHIAccess::BVHWrite;
			BufferRHI = RHICreateBuffer(NumBytes, Desc.Usage, 0, InitialAccess, CreateInfo);
		}
		else
		{
			check(0);
		}

	#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		RHIBindDebugLabelName(BufferRHI, InDebugName);
	#endif

		TRefCountPtr<FRDGPooledBuffer> PooledBuffer = new FRDGPooledBuffer(MoveTemp(BufferRHI), Desc, AlignedDesc.NumElements, InDebugName);
		AllocatedBuffers.Add(PooledBuffer);
		AllocatedBufferHashes.Add(BufferHash);
		check(PooledBuffer->GetRefCount() == 2);

		PooledBuffer->LastUsedFrame = FrameCounter;
		PooledBuffer->State.Access = InitialAccess;

		return PooledBuffer;
	}
}

void FRDGBufferPool::ReleaseDynamicRHI()
{
	AllocatedBuffers.Empty();
	AllocatedBufferHashes.Empty();
}

void FRDGBufferPool::TickPoolElements()
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

TGlobalResource<FRDGBufferPool> GRenderGraphResourcePool;

uint32 FRDGTransientRenderTarget::AddRef() const
{
	check(IsInRenderingThread());
	check(LifetimeState == ERDGTransientResourceLifetimeState::Allocated);
	return ++RefCount;
}

uint32 FRDGTransientRenderTarget::Release()
{
	check(IsInRenderingThread());
	check(RefCount > 0 && LifetimeState == ERDGTransientResourceLifetimeState::Allocated);
	const uint32 Refs = --RefCount;
	if (Refs == 0)
	{
		if (GRDGTransientResourceAllocator.IsValid())
		{
			GRDGTransientResourceAllocator.AddPendingDeallocation(this);
		}
		else
		{
			delete this;
		}
	}
	return Refs;
}

void FRDGTransientResourceAllocator::InitDynamicRHI()
{
	Allocator = RHICreateTransientResourceAllocator();
}

void FRDGTransientResourceAllocator::ReleaseDynamicRHI()
{
	if (Allocator)
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		ReleasePendingDeallocations();
		PendingDeallocationList.Empty();

		for (FRDGTransientRenderTarget* RenderTarget : DeallocatedList)
		{
			delete RenderTarget;
		}
		DeallocatedList.Empty();

		Allocator->Flush(RHICmdList);
		
		// Allocator->Flush() enqueues some lambdas on the command list, so make sure they are executed
		// before the allocator is deleted.
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);

		Allocator->Release(RHICmdList);
		Allocator = nullptr;
	}
}

TRefCountPtr<FRDGTransientRenderTarget> FRDGTransientResourceAllocator::AllocateRenderTarget(FRHITransientTexture* Texture)
{
	check(Texture);

	FRDGTransientRenderTarget* RenderTarget = nullptr;

	if (!FreeList.IsEmpty())
	{
		RenderTarget = FreeList.Pop();
	}
	else
	{
		RenderTarget = new FRDGTransientRenderTarget();
	}

	RenderTarget->Texture = Texture;
	RenderTarget->Desc = Translate(Texture->CreateInfo);
	RenderTarget->Desc.DebugName = Texture->GetName();
	RenderTarget->LifetimeState = ERDGTransientResourceLifetimeState::Allocated;
	RenderTarget->GetRenderTargetItem().TargetableTexture = Texture->GetRHI();
	RenderTarget->GetRenderTargetItem().ShaderResourceTexture = Texture->GetRHI();
	InitAsWholeResource(RenderTarget->State, {});
	return RenderTarget;
}

void FRDGTransientResourceAllocator::Release(TRefCountPtr<FRDGTransientRenderTarget>&& RenderTarget, FRDGPassHandle PassHandle)
{
	check(RenderTarget);

	if (RenderTarget->GetRefCount() == 1)
	{
		Allocator->DeallocateMemory(RenderTarget->Texture, PassHandle.GetIndex());
		RenderTarget->Reset();
		RenderTarget = nullptr;
	}
}

void FRDGTransientResourceAllocator::AddPendingDeallocation(FRDGTransientRenderTarget* RenderTarget)
{
	check(RenderTarget);
	check(RenderTarget->GetRefCount() == 0);

	if (RenderTarget->Texture)
	{
		RenderTarget->LifetimeState = ERDGTransientResourceLifetimeState::PendingDeallocation;
		PendingDeallocationList.Emplace(RenderTarget);
	}
	else
	{
		RenderTarget->LifetimeState = ERDGTransientResourceLifetimeState::Deallocated;
		DeallocatedList.Emplace(RenderTarget);
	}
}

void FRDGTransientResourceAllocator::ReleasePendingDeallocations()
{
	if (!PendingDeallocationList.IsEmpty())
	{
		FMemStack& MemStack = FMemStack::Get();
		FMemMark Mark(MemStack);

		TArray<FRHITransitionInfo, TMemStackAllocator<>> Transitions;
		Transitions.Reserve(PendingDeallocationList.Num());

		TArray<FRHITransientAliasingInfo, TMemStackAllocator<>> Aliases;
		Aliases.Reserve(PendingDeallocationList.Num());

		for (FRDGTransientRenderTarget* RenderTarget : PendingDeallocationList)
		{
			Allocator->DeallocateMemory(RenderTarget->Texture, 0);

			Aliases.Emplace(FRHITransientAliasingInfo::Discard(RenderTarget->Texture->GetRHI()));
			Transitions.Emplace(RenderTarget->Texture->GetRHI(), ERHIAccess::Unknown, ERHIAccess::Discard);

			RenderTarget->Reset();
			RenderTarget->LifetimeState = ERDGTransientResourceLifetimeState::Deallocated;
		}

		{
			const FRHITransition* Transition = RHICreateTransition(FRHITransitionCreateInfo(ERHIPipeline::Graphics, ERHIPipeline::Graphics, ERHITransitionCreateFlags::None, Transitions, Aliases));

			FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
			RHICmdList.BeginTransition(Transition);
			RHICmdList.EndTransition(Transition);
		}

		FreeList.Append(PendingDeallocationList);
		PendingDeallocationList.Reset();
	}

	if (!DeallocatedList.IsEmpty())
	{
		FreeList.Append(DeallocatedList);
		DeallocatedList.Reset();
	}
}

TGlobalResource<FRDGTransientResourceAllocator> GRDGTransientResourceAllocator;