// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D11VertexBuffer.cpp: D3D vertex buffer RHI implementation.
=============================================================================*/

#include "D3D11RHIPrivate.h"

TAutoConsoleVariable<int32> GCVarUseSharedKeyedMutex(
	TEXT("r.D3D11.UseSharedKeyMutex"),
	0,
	TEXT("If 1, BUF_Shared vertex / index buffer and TexCreate_Shared texture will be created\n")
	TEXT("with the D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX flag instead of D3D11_RESOURCE_MISC_SHARED (default).\n"),
	ECVF_Default);

FVertexBufferRHIRef FD3D11DynamicRHI::RHICreateVertexBuffer(uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	InUsage |= BUF_VertexBuffer;

	if (CreateInfo.bWithoutNativeResource)
	{
		return new FD3D11Buffer();
	}

	// Explicitly check that the size is nonzero before allowing CreateVertexBuffer to opaquely fail.
	check(Size > 0);

	D3D11_BUFFER_DESC Desc;
	ZeroMemory( &Desc, sizeof( D3D11_BUFFER_DESC ) );
	Desc.ByteWidth = Size;
	Desc.Usage = (InUsage & BUF_AnyDynamic) ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	Desc.CPUAccessFlags = (InUsage & BUF_AnyDynamic) ? D3D11_CPU_ACCESS_WRITE : 0;
	//Desc.MiscFlags = 0;
	//Desc.StructureByteStride = 0;

	if (InUsage & BUF_UnorderedAccess)
	{
		Desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
	}

	if (InUsage & BUF_ByteAddressBuffer)
	{
		Desc.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	}

	if (InUsage & BUF_DrawIndirect)
	{
		Desc.MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
	}

	if (InUsage & BUF_ShaderResource)
	{
		Desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
	}

	if (InUsage & BUF_Shared)
	{
		if (GCVarUseSharedKeyedMutex->GetInt() != 0)
		{
			Desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
		}
		else
		{
			Desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED;
		}
	}

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
		checkf(Size == CreateInfo.ResourceArray->GetResourceDataSize(),
			TEXT("DebugName: %s, GPU Size: %d, CPU Size: %d, Is Dynamic: %s"),
			CreateInfo.DebugName, Size, CreateInfo.ResourceArray->GetResourceDataSize(),
			InUsage & BUF_AnyDynamic ? TEXT("Yes") : TEXT("No"));

		InitData.pSysMem = CreateInfo.ResourceArray->GetResourceData();
		InitData.SysMemPitch = Size;
		InitData.SysMemSlicePitch = 0;
		pInitData = &InitData;
	}

	TRefCountPtr<ID3D11Buffer> VertexBufferResource;
	check(Desc.ByteWidth != 0); //tracking down an elusive bug.
	HRESULT Result = Direct3DDevice->CreateBuffer(&Desc, pInitData, VertexBufferResource.GetInitReference());
	check(Desc.ByteWidth != 0); //tracking down an elusive bug.
	if (FAILED(Result))
	{
		UE_LOG(LogD3D11RHI, Error, TEXT("D3DDevice failed CreateBuffer VB with ByteWidth=%d, BindFlags=0x%x Usage=%d, CPUAccess=0x%x, MiscFlags=0x%x"), Desc.ByteWidth, (uint32)Desc.BindFlags, (uint32)Desc.Usage, Desc.CPUAccessFlags, Desc.MiscFlags);
		VERIFYD3D11RESULT_EX(Result, Direct3DDevice);
	}

	if (CreateInfo.DebugName)
	{
		VertexBufferResource->SetPrivateData(WKPDID_D3DDebugObjectName, FCString::Strlen(CreateInfo.DebugName) + 1, TCHAR_TO_ANSI(CreateInfo.DebugName));
	}

	UpdateBufferStats(VertexBufferResource, true);

	if(CreateInfo.ResourceArray)
	{
		// Discard the resource array's contents.
		CreateInfo.ResourceArray->Discard();
	}

	return new FD3D11Buffer(VertexBufferResource, Size, InUsage, 0);
}

FVertexBufferRHIRef FD3D11DynamicRHI::CreateVertexBuffer_RenderThread(
	class FRHICommandListImmediate& RHICmdList,
	uint32 Size,
	uint32 InUsage,
	ERHIAccess InResourceState,
	FRHIResourceCreateInfo& CreateInfo)
{
	return RHICreateVertexBuffer(Size, InUsage, InResourceState, CreateInfo);
}

void FD3D11DynamicRHI::RHICopyVertexBuffer(FRHIVertexBuffer* SourceBufferRHI, FRHIVertexBuffer* DestBufferRHI)
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
