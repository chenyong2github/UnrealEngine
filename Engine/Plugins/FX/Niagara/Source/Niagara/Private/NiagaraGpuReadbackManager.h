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
	void EnqueueReadback(FRHICommandList& RHICmdList, FRHIVertexBuffer* Buffer, FCompletionCallback Callback);

	// Enqueue a readback of multiple buffers
	void EnqueueReadbacks(FRHICommandList& RHICmdList, TConstArrayView<FRHIVertexBuffer*> Buffers, FCompletionCallback Callback);

private:
	TQueue<FPendingReadback> PendingReadbacks;
};
