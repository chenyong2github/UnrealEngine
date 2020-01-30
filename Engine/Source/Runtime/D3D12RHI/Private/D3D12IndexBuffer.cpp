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

void FD3D12IndexBuffer::Rename(FD3D12ResourceLocation& NewLocation)
{
	FD3D12ResourceLocation::TransferOwnership(ResourceLocation, NewLocation);
}

void FD3D12IndexBuffer::RenameLDAChain(FD3D12ResourceLocation& NewLocation)
{
	// Dynamic buffers use cross-node resources.
	ensure(GetUsage() & BUF_AnyDynamic);
	Rename(NewLocation);

	if (GNumExplicitGPUsForRendering > 1)
	{
		// This currently crashes at exit time because NewLocation isn't tracked in the right allocator.
		ensure(IsHeadLink());
		ensure(GetParentDevice() == NewLocation.GetParentDevice());

		// Update all of the resources in the LDA chain to reference this cross-node resource
		for (FD3D12IndexBuffer* NextBuffer = GetNextObject(); NextBuffer; NextBuffer = NextBuffer->GetNextObject())
		{
			FD3D12ResourceLocation::ReferenceNode(NextBuffer->GetParentDevice(), NextBuffer->ResourceLocation, ResourceLocation);
		}
	}
}

void FD3D12IndexBuffer::Swap(FD3D12IndexBuffer& Other)
{
	check(!LockedData.bLocked && !Other.LockedData.bLocked);
	FRHIIndexBuffer::Swap(Other);
	FD3D12BaseShaderResource::Swap(Other);
	FD3D12TransientResource::Swap(Other);
	FD3D12LinkedAdapterObject<FD3D12IndexBuffer>::Swap(Other);
}

void FD3D12IndexBuffer::ReleaseUnderlyingResource()
{
	check(!LockedData.bLocked && ResourceLocation.IsValid());
	UpdateBufferStats<FD3D12IndexBuffer>(&ResourceLocation, false);
	ResourceLocation.Clear();
	FRHIIndexBuffer::ReleaseUnderlyingResource();
	FD3D12IndexBuffer* NextIB = GetNextObject();
	if (NextIB)
	{
		NextIB->ReleaseUnderlyingResource();
	}
}

FIndexBufferRHIRef FD3D12DynamicRHI::RHICreateIndexBuffer(uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	if (CreateInfo.bWithoutNativeResource)
	{
		return new FD3D12IndexBuffer();
	}

	const D3D12_RESOURCE_DESC Desc = CreateIndexBufferResourceDesc(Size, InUsage);
	const uint32 Alignment = 4;

	FD3D12IndexBuffer* Buffer = GetAdapter().CreateRHIBuffer<FD3D12IndexBuffer>(nullptr, Desc, Alignment, Stride, Size, InUsage, CreateInfo);
	if (Buffer->ResourceLocation.IsTransient())
	{
		// TODO: this should ideally be set in platform-independent code, since this tracking is for the high level
		Buffer->SetCommitted(false);
	}

	return Buffer;
}

void* FD3D12DynamicRHI::RHILockIndexBuffer(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	return LockBuffer(&RHICmdList, FD3D12DynamicRHI::ResourceCast(IndexBufferRHI), Offset, Size, LockMode);
}

void FD3D12DynamicRHI::RHIUnlockIndexBuffer(FRHICommandListImmediate& RHICmdList, FRHIIndexBuffer* IndexBufferRHI)
{
	UnlockBuffer(&RHICmdList, FD3D12DynamicRHI::ResourceCast(IndexBufferRHI));
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

FIndexBufferRHIRef FD3D12DynamicRHI::CreateIndexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	if (CreateInfo.bWithoutNativeResource)
	{
		return new FD3D12IndexBuffer();
	}

	const D3D12_RESOURCE_DESC Desc = CreateIndexBufferResourceDesc(Size, InUsage);
	const uint32 Alignment = 4;

	FD3D12IndexBuffer* Buffer = GetAdapter().CreateRHIBuffer<FD3D12IndexBuffer>(&RHICmdList, Desc, Alignment, Stride, Size, InUsage, CreateInfo);
	if (Buffer->ResourceLocation.IsTransient())
	{
		// TODO: this should ideally be set in platform-independent code, since this tracking is for the high level
		Buffer->SetCommitted(false);
	}

	return Buffer;
}

FIndexBufferRHIRef FD3D12DynamicRHI::CreateAndLockIndexBuffer_RenderThread(class FRHICommandListImmediate& RHICmdList, uint32 Stride, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer)
{
	const D3D12_RESOURCE_DESC Desc = CreateIndexBufferResourceDesc(Size, InUsage);
	const uint32 Alignment = 4;

	FD3D12IndexBuffer* Buffer = GetAdapter().CreateRHIBuffer<FD3D12IndexBuffer>(&RHICmdList, Desc, Alignment, Stride, Size, InUsage, CreateInfo);
	if (Buffer->ResourceLocation.IsTransient())
	{
		// TODO: this should ideally be set in platform-independent code, since this tracking is for the high level
		Buffer->SetCommitted(false);
	}

	OutDataBuffer = LockBuffer(&RHICmdList, Buffer, 0, Size, RLM_WriteOnly);

	return Buffer;
}
