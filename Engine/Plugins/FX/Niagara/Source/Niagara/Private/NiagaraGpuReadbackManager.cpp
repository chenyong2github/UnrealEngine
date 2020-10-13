// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraGpuReadbackManager.h"

#include "RHI.h"

// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
Class used help realtime debug Gpu Compute simulations
==============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "RHICommandList.h"

FNiagaraGpuReadbackManager::FNiagaraGpuReadbackManager()
{
}

FNiagaraGpuReadbackManager::~FNiagaraGpuReadbackManager()
{
}

void FNiagaraGpuReadbackManager::Tick()
{
	check(IsInRenderingThread());

	TArray<TPair<void*, uint32>, TInlineAllocator<1>> ReadbackData;
	while (FPendingReadback * Readback = PendingReadbacks.Peek())
	{
		// When we hit the first incomplete readback the rest will also be incomplete as we assume chronological insertion order
		if (!Readback->Fence->Poll())
		{
			break;
		}

		// Gather data an execute the callback
		for (const TPair<FStagingBufferRHIRef, uint32>& StagingBuffer : Readback->StagingBuffers)
		{
			void* DataPtr = StagingBuffer.Key->Lock(0, StagingBuffer.Value);
			ensure(DataPtr);
			ReadbackData.Emplace(DataPtr, StagingBuffer.Value);
		}

		Readback->Callback(MakeArrayView(ReadbackData));

		for (const TPair<FStagingBufferRHIRef, uint32>& StagingBuffer : Readback->StagingBuffers)
		{
			StagingBuffer.Key->Unlock();
		}

		ReadbackData.Reset();

		// Remove the readback as it's complete
		PendingReadbacks.Pop();
	}
}

void FNiagaraGpuReadbackManager::EnqueueReadback(FRHICommandList& RHICmdList, FRHIVertexBuffer* Buffer, FCompletionCallback Callback)
{
	static const FName FenceName(TEXT("NiagaraGpuReadback"));

	check(IsInRenderingThread());

	FPendingReadback Readback;
	Readback.StagingBuffers.Emplace(RHICreateStagingBuffer(), Buffer->GetSize());
	Readback.Fence = RHICreateGPUFence(FenceName);
	Readback.Callback = Callback;

	RHICmdList.CopyToStagingBuffer(Buffer, Readback.StagingBuffers[0].Key, 0, Readback.StagingBuffers[0].Value);

	Readback.Fence->Clear();
	RHICmdList.WriteGPUFence(Readback.Fence);

	PendingReadbacks.Enqueue(Readback);
}

void FNiagaraGpuReadbackManager::EnqueueReadbacks(FRHICommandList& RHICmdList, TConstArrayView<FRHIVertexBuffer*> Buffers, FCompletionCallback Callback)
{
	static const FName FenceName(TEXT("NiagaraGpuReadback"));

	check(IsInRenderingThread());

	FPendingReadback Readback;
	for (FRHIVertexBuffer* Buffer : Buffers)
	{
		const auto& ReadbackData = Readback.StagingBuffers.Emplace_GetRef(RHICreateStagingBuffer(), Buffer->GetSize());
		RHICmdList.CopyToStagingBuffer(Buffer, ReadbackData.Key, 0, ReadbackData.Value);
	}
	Readback.Fence = RHICreateGPUFence(FenceName);
	Readback.Callback = Callback;

	Readback.Fence->Clear();
	RHICmdList.WriteGPUFence(Readback.Fence);

	PendingReadbacks.Enqueue(Readback);
}
