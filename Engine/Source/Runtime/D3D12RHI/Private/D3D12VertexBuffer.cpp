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

void FD3D12VertexBuffer::Rename(FD3D12ResourceLocation& NewLocation)
{
	FD3D12ResourceLocation::TransferOwnership(ResourceLocation, NewLocation);

	if (DynamicSRV != nullptr)
	{
		DynamicSRV->Rename(ResourceLocation);
	}
}

void FD3D12VertexBuffer::RenameLDAChain(FD3D12ResourceLocation& NewLocation)
{
	ensure(GetUsage() & BUF_AnyDynamic);
	Rename(NewLocation);

	if (GNumExplicitGPUsForRendering > 1)
	{
		// Mutli-GPU support : renaming the LDA only works if we start we the head link. Otherwise Rename() must be used per GPU.
		ensure(IsHeadLink());
		ensure(GetParentDevice() == NewLocation.GetParentDevice());

		// Update all of the resources in the LDA chain to reference this cross-node resource
		for (FD3D12VertexBuffer* NextBuffer = GetNextObject(); NextBuffer; NextBuffer = NextBuffer->GetNextObject())
		{
			FD3D12ResourceLocation::ReferenceNode(NextBuffer->GetParentDevice(), NextBuffer->ResourceLocation, ResourceLocation);

			if (NextBuffer->DynamicSRV)
			{
				NextBuffer->DynamicSRV->Rename(NextBuffer->ResourceLocation);
			}
		}
	}
}

void FD3D12VertexBuffer::Swap(FD3D12VertexBuffer& Other)
{
	check(!LockedData.bLocked && !Other.LockedData.bLocked);
	FRHIVertexBuffer::Swap(Other);
	FD3D12BaseShaderResource::Swap(Other);
	FD3D12TransientResource::Swap(Other);
	FD3D12LinkedAdapterObject<FD3D12VertexBuffer>::Swap(Other);
	::Swap(DynamicSRV, Other.DynamicSRV);
}

void FD3D12VertexBuffer::ReleaseUnderlyingResource()
{
	check(!LockedData.bLocked && ResourceLocation.IsValid());
	UpdateBufferStats<FD3D12VertexBuffer>(&ResourceLocation, false);
	ResourceLocation.Clear();
	FRHIVertexBuffer::ReleaseUnderlyingResource();
	FD3D12VertexBuffer* NextVB = GetNextObject();
	if (NextVB)
	{
		NextVB->ReleaseUnderlyingResource();
	}
}

FVertexBufferRHIRef FD3D12DynamicRHI::RHICreateVertexBuffer(uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{
	if (CreateInfo.bWithoutNativeResource)
	{
		return new FD3D12VertexBuffer();
	}

	const D3D12_RESOURCE_DESC Desc = CreateVertexBufferResourceDesc(Size, InUsage);
	const uint32 Alignment = 4;

	FD3D12VertexBuffer* Buffer = GetAdapter().CreateRHIBuffer<FD3D12VertexBuffer>(nullptr, Desc, Alignment, 0, Size, InUsage, CreateInfo);
	if (Buffer->ResourceLocation.IsTransient() )
	{
		// TODO: this should ideally be set in platform-independent code, since this tracking is for the high level
		Buffer->SetCommitted(false);
	}

	return Buffer;
}

void* FD3D12DynamicRHI::RHILockVertexBuffer(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	return LockBuffer(&RHICmdList, FD3D12DynamicRHI::ResourceCast(VertexBufferRHI), Offset, Size, LockMode);
}

void FD3D12DynamicRHI::RHIUnlockVertexBuffer(FRHICommandListImmediate& RHICmdList, FRHIVertexBuffer* VertexBufferRHI)
{
	UnlockBuffer(&RHICmdList, FD3D12DynamicRHI::ResourceCast(VertexBufferRHI));
}

FVertexBufferRHIRef FD3D12DynamicRHI::CreateVertexBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo)
{	
	if (CreateInfo.bWithoutNativeResource)
	{
		return new FD3D12VertexBuffer();
	}

	const D3D12_RESOURCE_DESC Desc = CreateVertexBufferResourceDesc(Size, InUsage);
	const uint32 Alignment = 4;

	FD3D12VertexBuffer* Buffer = GetAdapter().CreateRHIBuffer<FD3D12VertexBuffer>(&RHICmdList, Desc, Alignment, 0, Size, InUsage, CreateInfo);
	if (Buffer->ResourceLocation.IsTransient())
	{
		// TODO: this should ideally be set in platform-independent code, since this tracking is for the high level
		Buffer->SetCommitted(false);
	}

	return Buffer;
}

void FD3D12DynamicRHI::RHICopyVertexBuffer(FRHIVertexBuffer* SourceBufferRHI, FRHIVertexBuffer* DestBufferRHI)
{
	FD3D12VertexBuffer*  SourceBuffer = FD3D12DynamicRHI::ResourceCast(SourceBufferRHI);
	FD3D12VertexBuffer*  DestBuffer = FD3D12DynamicRHI::ResourceCast(DestBufferRHI);

	while (SourceBuffer && DestBuffer)
	{
		FD3D12Device* Device = SourceBuffer->GetParentDevice();
		check(Device == DestBuffer->GetParentDevice());

		FD3D12Resource* pSourceResource = SourceBuffer->ResourceLocation.GetResource();
		D3D12_RESOURCE_DESC const& SourceBufferDesc = pSourceResource->GetDesc();

		FD3D12Resource* pDestResource = DestBuffer->ResourceLocation.GetResource();
		D3D12_RESOURCE_DESC const& DestBufferDesc = pDestResource->GetDesc();

		check(SourceBufferDesc.Width == DestBufferDesc.Width);
		check(SourceBuffer->GetSize() == DestBuffer->GetSize());

		FD3D12CommandContext& Context = Device->GetDefaultCommandContext();
		Context.numCopies++;
		Context.CommandListHandle->CopyResource(pDestResource->GetResource(), pSourceResource->GetResource());
		Context.CommandListHandle.UpdateResidency(pDestResource);
		Context.CommandListHandle.UpdateResidency(pSourceResource);

		DEBUG_EXECUTE_COMMAND_CONTEXT(Device->GetDefaultCommandContext());

		Device->RegisterGPUWork(1);

		SourceBuffer = SourceBuffer->GetNextObject();
		DestBuffer = DestBuffer->GetNextObject();
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
	FD3D12VertexBuffer*  SourceBuffer = RetrieveObject<FD3D12VertexBuffer>(SourceBufferRHI);
	FD3D12VertexBuffer*  DestBuffer = RetrieveObject<FD3D12VertexBuffer>(DestBufferRHI);

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
		FD3D12VertexBuffer*  SourceBuffer = RetrieveObject<FD3D12VertexBuffer>(Param.SourceBuffer);
		FD3D12VertexBuffer*  DestBuffer = RetrieveObject<FD3D12VertexBuffer>(Param.DestBuffer);
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

	auto TransitionResources = [](FD3D12CommandListHandle& CommandListHandle, FLocalResourceArray& SortedResources, EBatchCopyState State)
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
				CommandListHandle.AddTransitionBarrier(Resource, CurrentState, DesiredState, Subresource);
			}
			else
			{
				FD3D12DynamicRHI::TransitionResource(CommandListHandle, Resource, DesiredState, Subresource);
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
		FD3D12VertexBuffer*  SourceBuffer = RetrieveObject<FD3D12VertexBuffer>(Param.SourceBuffer);
		FD3D12VertexBuffer*  DestBuffer = RetrieveObject<FD3D12VertexBuffer>(Param.DestBuffer);
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

		Device->RegisterGPUWork(1);
	}

	// Transition buffers back to default readable state

	TransitionResources(CommandListHandle, SrcBuffers, EBatchCopyState::FinalizeSource);
	TransitionResources(CommandListHandle, DstBuffers, EBatchCopyState::FinalizeDest);
}
#endif // D3D12_RHI_RAYTRACING

FVertexBufferRHIRef FD3D12DynamicRHI::CreateAndLockVertexBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, uint32 Size, uint32 InUsage, FRHIResourceCreateInfo& CreateInfo, void*& OutDataBuffer)
{
	const D3D12_RESOURCE_DESC Desc = CreateVertexBufferResourceDesc(Size, InUsage);
	const uint32 Alignment = 4;

	FD3D12VertexBuffer* Buffer = GetAdapter().CreateRHIBuffer<FD3D12VertexBuffer>(nullptr, Desc, Alignment, 0, Size, InUsage, CreateInfo);
	if (Buffer->ResourceLocation.IsTransient())
	{
		// TODO: this should ideally be set in platform-independent code, since this tracking is for the high level
		Buffer->SetCommitted(false);
	}
	OutDataBuffer = LockBuffer(&RHICmdList, Buffer, 0, Size, RLM_WriteOnly);

	return Buffer;
}
