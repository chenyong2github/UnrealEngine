// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D12VertexBuffer.cpp: D3D vertex buffer RHI implementation.
	=============================================================================*/

#include "D3D12RHIPrivate.h"

D3D12_RESOURCE_DESC CreateVertexBufferResourceDesc(uint32 Size, uint32 InUsage)
{
	// Describe the vertex buffer.
	D3D12_RESOURCE_DESC Desc = CD3DX12_RESOURCE_DESC::Buffer(Size);

	if (InUsage & BUF_UnorderedAccess)
	{
		Desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		static bool bRequiresRawView = (GMaxRHIFeatureLevel < ERHIFeatureLevel::SM5);
		if (bRequiresRawView)
		{
			// Force the buffer to be a raw, byte address buffer
			InUsage |= BUF_ByteAddressBuffer;
		}
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

FD3D12VertexBuffer::~FD3D12VertexBuffer()
{
	if (ResourceLocation.GetResource() != nullptr)
	{
		UpdateBufferStats<FD3D12VertexBuffer>(&ResourceLocation, false);
	}
}

void FD3D12VertexBuffer::Swap(FD3D12VertexBuffer& Other)
{
	check(!LockedData.bLocked && !Other.LockedData.bLocked);
	FRHIVertexBuffer::Swap(Other);
	FD3D12BaseShaderResource::Swap(Other);
	FD3D12TransientResource::Swap(Other);
	FD3D12LinkedAdapterObject<FD3D12Buffer>::Swap(Other);
}

void FD3D12VertexBuffer::ReleaseUnderlyingResource()
{
	UpdateBufferStats<FD3D12VertexBuffer>(&ResourceLocation, false);
	FRHIVertexBuffer::ReleaseUnderlyingResource();
	FD3D12Buffer::ReleaseUnderlyingResource();
}

FVertexBufferRHIRef FD3D12DynamicRHI::RHICreateVertexBuffer(uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	if (CreateInfo.bWithoutNativeResource)
	{
		return GetAdapter().CreateLinkedObject<FD3D12VertexBuffer>(CreateInfo.GPUMask, [](FD3D12Device* Device)
			{
				return new FD3D12VertexBuffer();
			});
	}

	const D3D12_RESOURCE_DESC Desc = CreateVertexBufferResourceDesc(Size, InUsage);
	const uint32 Alignment = 4;

	FD3D12VertexBuffer* Buffer = GetAdapter().CreateRHIBuffer<FD3D12VertexBuffer>(nullptr, Desc, Alignment, 0, Size, InUsage, ED3D12ResourceStateMode::Default, CreateInfo);
	if (Buffer->ResourceLocation.IsTransient() )
	{
		// TODO: this should ideally be set in platform-independent code, since this tracking is for the high level
		Buffer->SetCommitted(false);
	}

	return Buffer;
}

void* FD3D12DynamicRHI::RHILockVertexBuffer(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	FD3D12VertexBuffer* Buffer = FD3D12DynamicRHI::ResourceCast(VertexBufferRHI);
	return LockBuffer(&RHICmdList, Buffer, Buffer->GetSize(), Buffer->GetUsage(), Offset, Size, LockMode);
}

void FD3D12DynamicRHI::RHIUnlockVertexBuffer(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI)
{
	FD3D12VertexBuffer* Buffer = FD3D12DynamicRHI::ResourceCast(VertexBufferRHI);
	UnlockBuffer(&RHICmdList, Buffer, Buffer->GetUsage());
}

FVertexBufferRHIRef FD3D12DynamicRHI::CreateVertexBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{	
	if (CreateInfo.bWithoutNativeResource)
	{
		return GetAdapter().CreateLinkedObject<FD3D12VertexBuffer>(CreateInfo.GPUMask, [](FD3D12Device* Device)
			{
				return new FD3D12VertexBuffer();
			});
	}

	const D3D12_RESOURCE_DESC Desc = CreateVertexBufferResourceDesc(Size, InUsage);
	const uint32 Alignment = 4;

	FD3D12VertexBuffer* Buffer = GetAdapter().CreateRHIBuffer<FD3D12VertexBuffer>(&RHICmdList, Desc, Alignment, 0, Size, InUsage, ED3D12ResourceStateMode::Default, CreateInfo);
	if (Buffer->ResourceLocation.IsTransient())
	{
		// TODO: this should ideally be set in platform-independent code, since this tracking is for the high level
		Buffer->SetCommitted(false);
	}

	return Buffer;
}

void FD3D12DynamicRHI::RHICopyVertexBuffer(FRHIVertexBuffer* SourceBufferRHI, FRHIVertexBuffer* DestBufferRHI)
{
	FD3D12VertexBuffer* SrcBuffer = FD3D12DynamicRHI::ResourceCast(SourceBufferRHI);
	FD3D12VertexBuffer* DstBuffer = FD3D12DynamicRHI::ResourceCast(DestBufferRHI);
	check(SrcBuffer->GetSize() == DstBuffer->GetSize());

	FD3D12Buffer* SourceBuffer = SrcBuffer;
	FD3D12Buffer* DestBuffer = DstBuffer;

	for (FD3D12Buffer::FDualLinkedObjectIterator It(SourceBuffer, DestBuffer); It; ++It)
	{
		SourceBuffer = It.GetFirst();
		DestBuffer = It.GetSecond();

		FD3D12Device* Device = SourceBuffer->GetParentDevice();
		check(Device == DestBuffer->GetParentDevice());

		FD3D12Resource* pSourceResource = SourceBuffer->ResourceLocation.GetResource();
		D3D12_RESOURCE_DESC const& SourceBufferDesc = pSourceResource->GetDesc();

		FD3D12Resource* pDestResource = DestBuffer->ResourceLocation.GetResource();
		D3D12_RESOURCE_DESC const& DestBufferDesc = pDestResource->GetDesc();

		check(SourceBufferDesc.Width == DestBufferDesc.Width);

		FD3D12CommandContext& Context = Device->GetDefaultCommandContext();
		Context.numCopies++;
		Context.CommandListHandle->CopyResource(pDestResource->GetResource(), pSourceResource->GetResource());
		Context.CommandListHandle.UpdateResidency(pDestResource);
		Context.CommandListHandle.UpdateResidency(pSourceResource);

		Context.ConditionalFlushCommandList();

		DEBUG_EXECUTE_COMMAND_CONTEXT(Device->GetDefaultCommandContext());

		Device->RegisterGPUWork(1);
	}
}

void FD3D12DynamicRHI::RHITransferVertexBufferUnderlyingResource(FRHIVertexBuffer* DestVertexBuffer, FRHIVertexBuffer* SrcVertexBuffer)
{
	check(DestVertexBuffer);
	FD3D12VertexBuffer* Dest = ResourceCast(DestVertexBuffer);
	if (!SrcVertexBuffer)
	{
		Dest->ReleaseUnderlyingResource();
	}
	else
	{
		FD3D12VertexBuffer* Src = ResourceCast(SrcVertexBuffer);
		Dest->Swap(*Src);
	}
}

#if D3D12_RHI_RAYTRACING
void FD3D12CommandContext::RHICopyBufferRegion(FRHIVertexBuffer* DestBufferRHI, uint64 DstOffset, FRHIVertexBuffer* SourceBufferRHI, uint64 SrcOffset, uint64 NumBytes)
{
	FD3D12Buffer* SourceBuffer = RetrieveObject<FD3D12Buffer>(SourceBufferRHI);
	FD3D12Buffer* DestBuffer = RetrieveObject<FD3D12Buffer>(DestBufferRHI);

	FD3D12Device* Device = SourceBuffer->GetParentDevice();
	check(Device == DestBuffer->GetParentDevice());
	check(Device == GetParentDevice());

	FD3D12Resource* pSourceResource = SourceBuffer->ResourceLocation.GetResource();
	D3D12_RESOURCE_DESC const& SourceBufferDesc = pSourceResource->GetDesc();

	FD3D12Resource* pDestResource = DestBuffer->ResourceLocation.GetResource();
	D3D12_RESOURCE_DESC const& DestBufferDesc = pDestResource->GetDesc();

	checkf(pSourceResource != pDestResource, TEXT("CopyBufferRegion cannot be used on the same resource. This can happen when both the source and the dest are suballocated from the same resource."));

	check(DstOffset + NumBytes <= DestBufferDesc.Width);
	check(SrcOffset + NumBytes <= SourceBufferDesc.Width);

	numCopies++;

	FConditionalScopeResourceBarrier ScopeResourceBarrierSource(CommandListHandle, pSourceResource, D3D12_RESOURCE_STATE_COPY_SOURCE, 0);
	FConditionalScopeResourceBarrier ScopeResourceBarrierDest(CommandListHandle, pDestResource, D3D12_RESOURCE_STATE_COPY_DEST, 0);
	CommandListHandle.FlushResourceBarriers();

	CommandListHandle->CopyBufferRegion(pDestResource->GetResource(), DestBuffer->ResourceLocation.GetOffsetFromBaseOfResource() + DstOffset, pSourceResource->GetResource(), SourceBuffer->ResourceLocation.GetOffsetFromBaseOfResource() + SrcOffset, NumBytes);
	CommandListHandle.UpdateResidency(pDestResource);
	CommandListHandle.UpdateResidency(pSourceResource);

	ConditionalFlushCommandList();

	Device->RegisterGPUWork(1);
}

void FD3D12CommandContext::RHICopyBufferRegions(const TArrayView<const FCopyBufferRegionParams> Params)
{
	// Batched buffer copy finds unique source and destination buffer resources, performs transitions
	// to copy source / dest state, then performs copies and finally restores original state.

	using FLocalResourceArray = TArray<FD3D12Resource*, TInlineAllocator<16, TMemStackAllocator<>>>;
	FLocalResourceArray SrcBuffers;
	FLocalResourceArray DstBuffers;

	SrcBuffers.Reserve(Params.Num());
	DstBuffers.Reserve(Params.Num());

	// Transition buffers to copy states
	for (auto& Param : Params)
	{
		FD3D12Buffer* SourceBuffer = RetrieveObject<FD3D12Buffer>(Param.SourceBuffer);
		FD3D12Buffer* DestBuffer = RetrieveObject<FD3D12Buffer>(Param.DestBuffer);
		check(SourceBuffer);
		check(DestBuffer);

		FD3D12Device* Device = SourceBuffer->GetParentDevice();
		check(Device == DestBuffer->GetParentDevice());
		check(Device == GetParentDevice());

		FD3D12Resource* pSourceResource = SourceBuffer->ResourceLocation.GetResource();
		FD3D12Resource* pDestResource = DestBuffer->ResourceLocation.GetResource();

		checkf(pSourceResource != pDestResource, TEXT("CopyBufferRegion cannot be used on the same resource. This can happen when both the source and the dest are suballocated from the same resource."));

		SrcBuffers.Add(pSourceResource);
		DstBuffers.Add(pDestResource);
	}

	Algo::Sort(SrcBuffers);
	Algo::Sort(DstBuffers);

	enum class EBatchCopyState
	{
		CopySource,
		CopyDest,
		FinalizeSource,
		FinalizeDest,
	};

	auto TransitionResources = [](FD3D12CommandListHandle& InCommandListHandle, FLocalResourceArray& SortedResources, EBatchCopyState State)
	{
		const uint32 Subresource = 0; // Buffers only have one subresource

		FD3D12Resource* PrevResource = nullptr;
		for (FD3D12Resource* Resource : SortedResources)
		{
			if (Resource == PrevResource) continue; // Skip duplicate resource barriers

			const bool bUseDefaultState = !Resource->RequiresResourceStateTracking();

			D3D12_RESOURCE_STATES DesiredState = D3D12_RESOURCE_STATE_CORRUPT;
			D3D12_RESOURCE_STATES CurrentState = D3D12_RESOURCE_STATE_CORRUPT;
			switch (State)
			{
			case EBatchCopyState::CopySource:
				DesiredState = D3D12_RESOURCE_STATE_COPY_SOURCE;
				CurrentState = bUseDefaultState ? Resource->GetDefaultResourceState() : CurrentState;
				break;
			case EBatchCopyState::CopyDest:
				DesiredState = D3D12_RESOURCE_STATE_COPY_DEST;
				CurrentState = bUseDefaultState ? Resource->GetDefaultResourceState() : CurrentState;
				break;
			case EBatchCopyState::FinalizeSource:
				CurrentState = D3D12_RESOURCE_STATE_COPY_SOURCE;
				DesiredState = bUseDefaultState ? Resource->GetDefaultResourceState() : D3D12_RESOURCE_STATE_GENERIC_READ;
				break;
			case EBatchCopyState::FinalizeDest:
				CurrentState = D3D12_RESOURCE_STATE_COPY_DEST;
				DesiredState = bUseDefaultState ? Resource->GetDefaultResourceState() : D3D12_RESOURCE_STATE_GENERIC_READ;
				break;
			default:
				checkf(false, TEXT("Unexpected batch copy state"));
				break;
			}

			if (bUseDefaultState)
			{
				check(CurrentState != D3D12_RESOURCE_STATE_CORRUPT);
				InCommandListHandle.AddTransitionBarrier(Resource, CurrentState, DesiredState, Subresource);
			}
			else
			{
				FD3D12DynamicRHI::TransitionResource(InCommandListHandle, Resource, DesiredState, Subresource);
			}

			PrevResource = Resource;
		}
	};

	// Ensure that all previously pending barriers have been processed to avoid incorrect/conflicting transitions for non-tracked resources
	CommandListHandle.FlushResourceBarriers();

	TransitionResources(CommandListHandle, SrcBuffers, EBatchCopyState::CopySource);
	TransitionResources(CommandListHandle, DstBuffers, EBatchCopyState::CopyDest);

	// Issue all copy source/dest barriers before performing actual copies
	CommandListHandle.FlushResourceBarriers();

	for (auto& Param : Params)
	{
		FD3D12Buffer* SourceBuffer = RetrieveObject<FD3D12Buffer>(Param.SourceBuffer);
		FD3D12Buffer* DestBuffer = RetrieveObject<FD3D12Buffer>(Param.DestBuffer);
		uint64 SrcOffset = Param.SrcOffset;
		uint64 DstOffset = Param.DstOffset;
		uint64 NumBytes = Param.NumBytes;

		FD3D12Device* Device = SourceBuffer->GetParentDevice();
		check(Device == DestBuffer->GetParentDevice());

		FD3D12Resource* pSourceResource = SourceBuffer->ResourceLocation.GetResource();
		D3D12_RESOURCE_DESC const& SourceBufferDesc = pSourceResource->GetDesc();

		FD3D12Resource* pDestResource = DestBuffer->ResourceLocation.GetResource();
		D3D12_RESOURCE_DESC const& DestBufferDesc = pDestResource->GetDesc();

		check(DstOffset + NumBytes <= DestBufferDesc.Width);
		check(SrcOffset + NumBytes <= SourceBufferDesc.Width);

		numCopies++;

		CommandListHandle->CopyBufferRegion(pDestResource->GetResource(), DestBuffer->ResourceLocation.GetOffsetFromBaseOfResource() + DstOffset, pSourceResource->GetResource(), SourceBuffer->ResourceLocation.GetOffsetFromBaseOfResource() + SrcOffset, NumBytes);
		CommandListHandle.UpdateResidency(pDestResource);
		CommandListHandle.UpdateResidency(pSourceResource);

		ConditionalFlushCommandList();

		Device->RegisterGPUWork(1);
	}

	// Transition buffers back to default readable state

	TransitionResources(CommandListHandle, SrcBuffers, EBatchCopyState::FinalizeSource);
	TransitionResources(CommandListHandle, DstBuffers, EBatchCopyState::FinalizeDest);
}
#endif // D3D12_RHI_RAYTRACING

FVertexBufferRHIRef FD3D12DynamicRHI::CreateAndLockVertexBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 InUsage, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer)
{
	const D3D12_RESOURCE_DESC Desc = CreateVertexBufferResourceDesc(Size, InUsage);
	const uint32 Alignment = 4;

	FD3D12VertexBuffer* Buffer = GetAdapter().CreateRHIBuffer<FD3D12VertexBuffer>(nullptr, Desc, Alignment, 0, Size, InUsage, ED3D12ResourceStateMode::Default, CreateInfo);
	if (Buffer->ResourceLocation.IsTransient())
	{
		// TODO: this should ideally be set in platform-independent code, since this tracking is for the high level
		Buffer->SetCommitted(false);
	}
	OutDataBuffer = LockBuffer(&RHICmdList, Buffer, Buffer->GetSize(), Buffer->GetUsage(), 0, Size, RLM_WriteOnly);

	return Buffer;
}
