// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "NiagaraCommon.h"
#include "RHICommandList.h"

class FNiagaraGpuReadbackManager
{
protected:
	typedef TFunction<void(TConstArrayView<TPair<void*, uint32>>)> FCompletionCallback;
		
	struct FPendingReadback
	{
		TArray<TPair<FStagingBufferRHIRef, uint32>, TInlineAllocator<1>>	StagingBuffers;
		FGPUFenceRHIRef														Fence;
		FCompletionCallback													Callback;
	};

public:
	struct FBufferRequest
	{
		FRHIBuffer*	Buffer = nullptr;
		uint32		Offset = 0;
		uint32		Size = 0;
	};

public:
	FNiagaraGpuReadbackManager();
	~FNiagaraGpuReadbackManager();

	// Tick call which polls for completed readbacks
	void Tick();
	
private:
	// Internal tick impl
	void TickInternal(bool bAssumeGpuIdle);

public:
	// Wait for all pending readbacks to complete
	void WaitCompletion(FRHICommandListImmediate& RHICmdList);

	// Enqueue a readback of a single buffer
	void EnqueueReadback(FRHICommandList& RHICmdList, FRHIBuffer* Buffer, FCompletionCallback Callback);
	void EnqueueReadback(FRHICommandList& RHICmdList, FRHIBuffer* Buffer, uint32 Offset, uint32 NumBytes, FCompletionCallback Callback);

	// Enqueue a readback of multiple buffers
	void EnqueueReadbacks(FRHICommandList& RHICmdList, TConstArrayView<FRHIBuffer*> Buffers, FCompletionCallback Callback);
	void EnqueueReadbacks(FRHICommandList& RHICmdList, TConstArrayView<FBufferRequest> BufferRequest, FCompletionCallback Callback);

private:
	TQueue<FPendingReadback> PendingReadbacks;
};
