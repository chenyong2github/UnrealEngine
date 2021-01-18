// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D11VertexBuffer.cpp: D3D vertex buffer RHI implementation.
=============================================================================*/

#include "D3D11RHIPrivate.h"

void FD3D11DynamicRHI::RHICopyBuffer(FRHIBuffer* SourceBufferRHI, FRHIBuffer* DestBufferRHI)
{
	FD3D11Buffer* SourceBuffer = ResourceCast(SourceBufferRHI);
	FD3D11Buffer* DestBuffer = ResourceCast(DestBufferRHI);

	D3D11_BUFFER_DESC SourceBufferDesc;
	SourceBuffer->Resource->GetDesc(&SourceBufferDesc);
	
	D3D11_BUFFER_DESC DestBufferDesc;
	DestBuffer->Resource->GetDesc(&DestBufferDesc);

	check(SourceBufferDesc.ByteWidth == DestBufferDesc.ByteWidth);

	Direct3DDeviceIMContext->CopyResource(DestBuffer->Resource,SourceBuffer->Resource);

	GPUProfilingData.RegisterGPUWork(1);
}
