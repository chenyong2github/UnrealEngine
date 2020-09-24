// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D12IndexBuffer.cpp: D3D Index buffer RHI implementation.
=============================================================================*/

#include "D3D12RHIPrivate.h"

D3D12_RESOURCE_DESC CreateIndexBufferResourceDesc(uint32 Size, uint32 InUsage)
{
	// Describe the vertex buffer.
	D3D12_RESOURCE_DESC Desc = CD3DX12_RESOURCE_DESC::Buffer(Size);

	if (InUsage & BUF_UnorderedAccess)
	{
		Desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}

	if ((InUsage & BUF_ShaderResource) == 0)
	{
		Desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
	}

	if (InUsage & BUF_DrawIndirect)
	{
		Desc.Flags |= D3D12RHI_RESOURCE_FLAG_ALLOW_INDIRECT_BUFFER;
	}

	return Desc;
}

FD3D12IndexBuffer::~FD3D12IndexBuffer()
{
	if (ResourceLocation.IsValid())
	{
		UpdateBufferStats<FD3D12IndexBuffer>(&ResourceLocation, false);
	}
}

void FD3D12IndexBuffer::Swap(FD3D12IndexBuffer& Other)
{
	check(!LockedData.bLocked && !Other.LockedData.bLocked);
	FRHIIndexBuffer::Swap(Other);
	FD3D12BaseShaderResource::Swap(Other);
	FD3D12TransientResource::Swap(Other);
	FD3D12LinkedAdapterObject<FD3D12Buffer>::Swap(Other);
}

void FD3D12IndexBuffer::ReleaseUnderlyingResource()
{
	UpdateBufferStats<FD3D12IndexBuffer>(&ResourceLocation, false);
	FRHIIndexBuffer::ReleaseUnderlyingResource();
	FD3D12Buffer::ReleaseUnderlyingResource();
}

FIndexBufferRHIRef FD3D12DynamicRHI::RHICreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	if (CreateInfo.bWithoutNativeResource)
	{
		return GetAdapter().CreateLinkedObject<FD3D12IndexBuffer>(CreateInfo.GPUMask, [](FD3D12Device* Device)
			{
				return new FD3D12IndexBuffer();
			});
	}

	const D3D12_RESOURCE_DESC Desc = CreateIndexBufferResourceDesc(Size, InUsage);
	const uint32 Alignment = 4;

	FD3D12IndexBuffer* Buffer = GetAdapter().CreateRHIBuffer<FD3D12IndexBuffer>(nullptr, Desc, Alignment, Stride, Size, InUsage, ED3D12ResourceStateMode::Default, CreateInfo);
	if (Buffer->ResourceLocation.IsTransient())
	{
		// TODO: this should ideally be set in platform-independent code, since this tracking is for the high level
		Buffer->SetCommitted(false);
	}

	return Buffer;
}

void* FD3D12DynamicRHI::RHILockIndexBuffer(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	FD3D12IndexBuffer* Buffer = FD3D12DynamicRHI::ResourceCast(IndexBufferRHI);
	return LockBuffer(&RHICmdList, Buffer, Buffer->GetSize(), Buffer->GetUsage(), Offset, Size, LockMode);
}

void FD3D12DynamicRHI::RHIUnlockIndexBuffer(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBufferRHI)
{
	FD3D12IndexBuffer* Buffer = FD3D12DynamicRHI::ResourceCast(IndexBufferRHI);
	UnlockBuffer(&RHICmdList, Buffer, Buffer->GetUsage());
}

void FD3D12DynamicRHI::RHITransferIndexBufferUnderlyingResource(FRHIIndexBuffer* DestIndexBuffer, FRHIIndexBuffer* SrcIndexBuffer)
{
	check(DestIndexBuffer);
	FD3D12IndexBuffer* Dest = ResourceCast(DestIndexBuffer);
	if (!SrcIndexBuffer)
	{
		Dest->ReleaseUnderlyingResource();
	}
	else
	{
		FD3D12IndexBuffer* Src = ResourceCast(SrcIndexBuffer);
		Dest->Swap(*Src);
	}
}

FIndexBufferRHIRef FD3D12DynamicRHI::CreateIndexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	if (CreateInfo.bWithoutNativeResource)
	{
		return GetAdapter().CreateLinkedObject<FD3D12IndexBuffer>(CreateInfo.GPUMask, [](FD3D12Device* Device)
			{
				return new FD3D12IndexBuffer();
			});
	}

	const D3D12_RESOURCE_DESC Desc = CreateIndexBufferResourceDesc(Size, InUsage);
	const uint32 Alignment = 4;

	FD3D12IndexBuffer* Buffer = GetAdapter().CreateRHIBuffer<FD3D12IndexBuffer>(&RHICmdList, Desc, Alignment, Stride, Size, InUsage, ED3D12ResourceStateMode::Default, CreateInfo);
	if (Buffer->ResourceLocation.IsTransient())
	{
		// TODO: this should ideally be set in platform-independent code, since this tracking is for the high level
		Buffer->SetCommitted(false);
	}

	return Buffer;
}

FIndexBufferRHIRef FD3D12DynamicRHI::CreateAndLockIndexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer)
{
	const D3D12_RESOURCE_DESC Desc = CreateIndexBufferResourceDesc(Size, InUsage);
	const uint32 Alignment = 4;

	FD3D12IndexBuffer* Buffer = GetAdapter().CreateRHIBuffer<FD3D12IndexBuffer>(&RHICmdList, Desc, Alignment, Stride, Size, InUsage, ED3D12ResourceStateMode::Default, CreateInfo);
	if (Buffer->ResourceLocation.IsTransient())
	{
		// TODO: this should ideally be set in platform-independent code, since this tracking is for the high level
		Buffer->SetCommitted(false);
	}

	OutDataBuffer = LockBuffer(&RHICmdList, Buffer, Buffer->GetSize(), Buffer->GetUsage(), 0, Size, RLM_WriteOnly);

	return Buffer;
}
