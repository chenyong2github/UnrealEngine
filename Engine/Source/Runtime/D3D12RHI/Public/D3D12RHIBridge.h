// Copyright Epic Games, Inc. All Rights Reserved.

namespace D3D12RHI
{
	// Use these functions with care!

	// Returns ID3D12GraphicsCommandList* and ID3D12CommandQueue*
	D3D12RHI_API void GetGfxCommandListAndQueue(FRHICommandList& RHICmdList, void*& OutGfxCmdList, void*& OutCommandQueue);

	// Returns ID3D12CommandQueue* for copy operations
	D3D12RHI_API void GetCopyCommandQueue(FRHICommandList& RHICmdList, void*& OutCommandQueue);
}
