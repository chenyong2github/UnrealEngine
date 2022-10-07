// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12RHIPrivate.h"
#include "Windows.h"

extern bool D3D12RHI_ShouldCreateWithD3DDebug();

TLockFreePointerListUnordered<void, PLATFORM_CACHE_LINE_SIZE> FD3D12SyncPoint::MemoryPool;

FD3D12GPUFence::FD3D12GPUFence(FName InName)
	: FRHIGPUFence(InName)
{
	Clear();
}

void FD3D12GPUFence::Clear()
{
	SyncPoints.Reset();
	SyncPoints.AddDefaulted(FRHIGPUMask::All().GetNumActive());
}

bool FD3D12GPUFence::Poll() const
{
	return Poll(FRHIGPUMask::All());
}

bool FD3D12GPUFence::Poll(FRHIGPUMask GPUMask) const
{
	for (uint32 Index : GPUMask)
	{
		if (ensureMsgf(SyncPoints[Index], TEXT("Attempt to poll an FRHIGPUFence that was never issued.")) && !SyncPoints[Index]->IsComplete())
			return false;
	}

	return true;
}

void FD3D12GPUFence::WaitCPU()
{
	for (FD3D12SyncPointRef& SyncPoint : SyncPoints)
	{
		if (SyncPoint && !SyncPoint->IsComplete())
		{
			SyncPoint->Wait();
		}
	}
}

void FD3D12DynamicRHI::RHIWriteGPUFence_TopOfPipe(FRHICommandListBase& RHICmdList, FRHIGPUFence* FenceRHI)
{
	FD3D12GPUFence* Fence = FD3D12DynamicRHI::ResourceCast(FenceRHI);
	check(Fence);

	for (uint32 GPUIndex : RHICmdList.GetGPUMask())
	{
		checkf(Fence->SyncPoints[GPUIndex] == nullptr, TEXT("The fence for the current GPU node has already been issued."));
		Fence->SyncPoints[GPUIndex] = FD3D12SyncPoint::Create(ED3D12SyncPointType::GPUAndCPU);
	}

	FDynamicRHI::RHIWriteGPUFence_TopOfPipe(RHICmdList, FenceRHI);
}

void FD3D12CommandContext::RHIWriteGPUFence(FRHIGPUFence* FenceRHI)
{
	FD3D12GPUFence* Fence = FD3D12DynamicRHI::ResourceCast(FenceRHI);

	check(Fence);
	check(Fence->SyncPoints[GetGPUIndex()]);

	CloseCommandList(true);
	SignalSyncPoint(Fence->SyncPoints[GetGPUIndex()]);
	OpenCommandList();
}


FGPUFenceRHIRef FD3D12DynamicRHI::RHICreateGPUFence(const FName& Name)
{
	LLM_SCOPE_BYNAME(TEXT("RHIMisc/CreateGPUFence"));
	return new FD3D12GPUFence(Name);
}

FStagingBufferRHIRef FD3D12DynamicRHI::RHICreateStagingBuffer()
{
	// Don't know the device yet - will be decided at copy time (lazy creation)
	LLM_SCOPE_BYNAME(TEXT("RHIMisc/CreateStagingBuffer"));
	return new FD3D12StagingBuffer(nullptr);
}

void* FD3D12DynamicRHI::RHILockStagingBuffer(FRHIStagingBuffer* StagingBufferRHI, FRHIGPUFence* Fence, uint32 Offset, uint32 SizeRHI)
{
	FD3D12StagingBuffer* StagingBuffer = FD3D12DynamicRHI::ResourceCast(StagingBufferRHI);
	check(StagingBuffer);

	return StagingBuffer->Lock(Offset, SizeRHI);
}

void FD3D12DynamicRHI::RHIUnlockStagingBuffer(FRHIStagingBuffer* StagingBufferRHI)
{
	FD3D12StagingBuffer* StagingBuffer = FD3D12DynamicRHI::ResourceCast(StagingBufferRHI);
	check(StagingBuffer);
	StagingBuffer->Unlock();
}

// =============================================================================

FD3D12ManualFence::FD3D12ManualFence(FD3D12Adapter* InParent)
	: Parent(InParent)
{
	for (FD3D12Device* Device : Parent->GetDevices())
	{
		FFencePair& Pair = FencePairs.Emplace_GetRef();
		Pair.Context = &Device->GetDefaultCommandContext();

		VERIFYD3D12RESULT(Parent->GetD3DDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(Pair.Fence.GetInitReference())));
		Pair.Fence->SetName(TEXT("Manual Fence"));
	}
}

uint64 FD3D12ManualFence::GetCompletedFenceValue(bool bUpdateCachedFenceValue)
{
	if (bUpdateCachedFenceValue)
	{
		uint64 MinFenceValue = TNumericLimits<uint64>::Max();

		for (FFencePair& Pair : FencePairs)
		{
			MinFenceValue = FMath::Min(
				Pair.Fence->GetCompletedValue(),
				MinFenceValue
			);
		}

		CompletedFenceValue = MinFenceValue;
	}

	return CompletedFenceValue;
}

void FD3D12ManualFence::AdvanceFrame()
{
	check(IsInRenderingThread());

	const uint64 NextValue = NextFenceValue.Increment();
	FRHICommandListExecutor::GetImmediateCommandList().EnqueueLambda([this, NextValue](FRHICommandListImmediate&)
	{
		for (FFencePair& Pair : FencePairs)
		{
			Pair.Context->CloseCommandList(false);
		}

		for (FFencePair& Pair : FencePairs)
		{
			Pair.Context->SignalManualFence(Pair.Fence, NextValue);
		}

		for (FFencePair& Pair : FencePairs)
		{
			Pair.Context->OpenCommandList();
		}
	});
}
