// Copyright Epic Games, Inc. All Rights Reserved.

#include "Templates/Function.h"

namespace D3D12RHI
{
	// Use these functions with care!

	// Returns ID3D12GraphicsCommandList* and ID3D12CommandQueue*
	D3D12RHI_API void GetGfxCommandListAndQueue(FRHICommandList& RHICmdList, void*& OutGfxCmdList, void*& OutCommandQueue);

	// Execute code using the CopyCommandQueue interface in a thread safe way
	D3D12RHI_API void ExecuteCodeWithCopyCommandQueueUsage(TFunction<void(ID3D12CommandQueue*)>&& CodeToRun);
}
