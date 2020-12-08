// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D11RHIPrivate.h"

FStructuredBufferRHIRef FD3D11DynamicRHI::RHICreateStructuredBuffer(uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	InUsage |= BUF_StructuredBuffer;

	// Explicitly check that the size is nonzero before allowing CreateStructuredBuffer to opaquely fail.
	check(Size > 0);
	// Check for values that will cause D3D calls to fail
	check(Size / Stride > 0 && Size % Stride == 0);

	D3D11_BUFFER_DESC Desc;
	ZeroMemory( &Desc, sizeof( D3D11_BUFFER_DESC ) );
	Desc.ByteWidth = Size;
	Desc.Usage = (InUsage & BUF_AnyDynamic) ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
	Desc.BindFlags = 0;

	if(InUsage & BUF_ShaderResource)
	{
		// Setup bind flags so we can create a view to read from the buffer in a shader.
		Desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
	}

	if (InUsage & BUF_UnorderedAccess)
	{
		// Setup bind flags so we can create a writeable UAV to the buffer
		Desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
	}

	Desc.CPUAccessFlags = (InUsage & BUF_AnyDynamic) ? D3D11_CPU_ACCESS_WRITE : 0;
	Desc.MiscFlags = 0;

	if (InUsage & BUF_DrawIndirect)
	{
		Desc.MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
	}
	else
	{
		if (InUsage & BUF_ByteAddressBuffer)
		{
			Desc.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		}
		else
		{
			Desc.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		}
	}

	Desc.StructureByteStride = Stride;

	if (FPlatformMemory::SupportsFastVRAMMemory())
	{
		if (InUsage & BUF_FastVRAM)
		{
			FFastVRAMAllocator::GetFastVRAMAllocator()->AllocUAVBuffer(Desc);
		}
	}

	// If a resource array was provided for the resource, create the resource pre-populated
	D3D11_SUBRESOURCE_DATA InitData;
	D3D11_SUBRESOURCE_DATA* pInitData = NULL;
	if(CreateInfo.ResourceArray)
	{
		check(Size == CreateInfo.ResourceArray->GetResourceDataSize());
		InitData.pSysMem = CreateInfo.ResourceArray->GetResourceData();
		InitData.SysMemPitch = Size;
		InitData.SysMemSlicePitch = 0;
		pInitData = &InitData;
	}

	TRefCountPtr<ID3D11Buffer> StructuredBufferResource;
	VERIFYD3D11RESULT_EX(Direct3DDevice->CreateBuffer(&Desc,pInitData,StructuredBufferResource.GetInitReference()), Direct3DDevice);

	if( CreateInfo.DebugName )
	{
		StructuredBufferResource->SetPrivateData(WKPDID_D3DDebugObjectName, FCString::Strlen(CreateInfo.DebugName) + 1, TCHAR_TO_ANSI(CreateInfo.DebugName));
	}

	UpdateBufferStats(StructuredBufferResource, true);

	if(CreateInfo.ResourceArray)
	{
		// Discard the resource array's contents.
		CreateInfo.ResourceArray->Discard();
	}

	return new FD3D11Buffer(StructuredBufferResource, Size, InUsage, Stride);
}

FStructuredBufferRHIRef FD3D11DynamicRHI::CreateStructuredBuffer_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	uint32 Stride,
	uint32 Size,
	uint32 InUsage,
	ERHIAccess InResourceState,
	FRHIResourceCreateInfo& CreateInfo)
{
	return RHICreateStructuredBuffer(Stride, Size, InUsage, InResourceState, CreateInfo);
}
