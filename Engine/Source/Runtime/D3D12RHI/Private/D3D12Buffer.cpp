// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
D3D12Buffer.cpp: D3D Common code for buffers.
=============================================================================*/

#include "D3D12RHIPrivate.h"

FD3D12Buffer::~FD3D12Buffer()
{
	if (EnumHasAnyFlags(GetUsage(), EBufferUsageFlags::VertexBuffer) && GetParentDevice())
	{
		FD3D12CommandContext& DefaultContext = GetParentDevice()->GetDefaultCommandContext();
		DefaultContext.StateCache.ClearVertexBuffer(&ResourceLocation);
	}

	int64 BufferSize = ResourceLocation.GetSize();
	bool bTransient = ResourceLocation.IsTransient();
	if (!bTransient)
	{
		UpdateBufferStats((EBufferUsageFlags)GetUsage(), -BufferSize);
	}
}

struct FRHICommandUpdateBufferString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandUpdateBuffer"); }
};
struct FRHICommandUpdateBuffer final : public FRHICommand<FRHICommandUpdateBuffer, FRHICommandUpdateBufferString>
{
	FD3D12ResourceLocation Source;
	FD3D12ResourceLocation* Destination;
	uint32 NumBytes;
	uint32 DestinationOffset;

	FORCEINLINE_DEBUGGABLE FRHICommandUpdateBuffer(FD3D12ResourceLocation* InDest, FD3D12ResourceLocation& InSource, uint32 InDestinationOffset, uint32 InNumBytes)
		: Source(nullptr)
		, Destination(InDest)
		, NumBytes(InNumBytes)
		, DestinationOffset(InDestinationOffset)
	{
		FD3D12ResourceLocation::TransferOwnership(Source, InSource);
	}

	void Execute(FRHICommandListBase& CmdList)
	{
		FD3D12DynamicRHI::GetD3DRHI()->UpdateBuffer(Destination, DestinationOffset, &Source, 0, NumBytes);
	}
};

// This allows us to rename resources from the RenderThread i.e. all the 'hard' work of allocating a new resource
// is done in parallel and this small function is called to switch the resource to point to the correct location
// a the correct time.
struct FRHICommandRenameUploadBufferString
{
	static const TCHAR* TStr() { return TEXT("FRHICommandRenameUploadBuffer"); }
};
struct FRHICommandRenameUploadBuffer final : public FRHICommand<FRHICommandRenameUploadBuffer, FRHICommandRenameUploadBufferString>
{
	FD3D12Buffer* Resource;
	FD3D12ResourceLocation NewLocation;

	FORCEINLINE_DEBUGGABLE FRHICommandRenameUploadBuffer(FD3D12Buffer* InResource, FD3D12Device* Device)
		: Resource(InResource)
		, NewLocation(Device) 
	{}

	void Execute(FRHICommandListBase& CmdList)
	{
		// Clear the resource if still bound to make sure the SRVs are rebound again on next operation
		FD3D12CommandContext& Context = (FD3D12CommandContext&)(CmdList.IsImmediateAsyncCompute() ? CmdList.GetComputeContext().GetLowestLevelContext() : CmdList.GetContext().GetLowestLevelContext());
		Context.ConditionalClearShaderResource(&Resource->ResourceLocation);

		Resource->RenameLDAChain(NewLocation);
	}
};

struct FD3D12RHICommandInitializeBufferString
{
	static const TCHAR* TStr() { return TEXT("FD3D12RHICommandInitializeBuffer"); }
};
struct FD3D12RHICommandInitializeBuffer final : public FRHICommand<FD3D12RHICommandInitializeBuffer, FD3D12RHICommandInitializeBufferString>
{
	TRefCountPtr<FD3D12Buffer> Buffer;
	FD3D12ResourceLocation SrcResourceLoc;
	uint32 Size;
	D3D12_RESOURCE_STATES DestinationState;

	FORCEINLINE_DEBUGGABLE FD3D12RHICommandInitializeBuffer(TRefCountPtr<FD3D12Buffer>&& InBuffer, FD3D12ResourceLocation& InSrcResourceLoc, uint32 InSize, D3D12_RESOURCE_STATES InDestinationState)
		: Buffer(MoveTemp(InBuffer))
		, SrcResourceLoc(InSrcResourceLoc.GetParentDevice())
		, Size(InSize)
		, DestinationState(InDestinationState)
	{
		FD3D12ResourceLocation::TransferOwnership(SrcResourceLoc, InSrcResourceLoc);
	}

	void Execute(FRHICommandListBase& CmdList)
	{
		FD3D12CommandContext& CommandContext = (FD3D12CommandContext&)(CmdList.IsImmediateAsyncCompute() ? CmdList.GetComputeContext().GetLowestLevelContext() : CmdList.GetContext().GetLowestLevelContext());
#if ENABLE_RHI_VALIDATION && WITH_MGPU
		// Need to do a second level of indirection, to potentially go from FD3D12CommandContextRedirector to FD3D12CommandContext, when
		// -rhivalidation is enabled.  The issue arises because GetLowestLevelContext on FValidationContext returns the contained
		// FD3D12CommandContextRedirector, and doesn't attempt to call GetLowestLevelContext on that to get the physical FD3D12CommandContext
		// required here.  Only when both validation and MGPU are present is it possible for this second level of indirection to be required.
		// The call to "GetLowestLevelContext" is a nop (returns *this) if already a leaf command context.
		//
		// I thought about changing the original FValidationContext::GetLowestLevelContext implementation to attempt a second indirection there,
		// but I wasn't sure what other side effects there might be for that change.  Here, the change is extremely safe, because the
		// CommandContext passed to ExecuteOnCommandContext is solely used for a validation assert when WITH_MGPU is true, not actual rendering.
		// So it can't affect behavior, it's just to avoid a spurious assert.
		ExecuteOnCommandContext((FD3D12CommandContext&)CommandContext.GetLowestLevelContext());
#else
		ExecuteOnCommandContext(CommandContext);
#endif
	}

	void ExecuteOnCommandContext(FD3D12CommandContext& CommandContext)
	{
#if WITH_MGPU
		// With multiple GPU support, we need to issue staging buffer upload commands on the command context for the same device (GPU) that the
		// resource is on.  So we always use the default command context per GPU, and ignore the command context passed in.  In practice, the
		// caller will already be passing the default command context in, but if we run into a situation where that's not the case, it would
		// require some sort of higher level refactor of the code (for example, moving the linked object iterator loop to a higher level, or
		// introducing a cross GPU fence sync at the end of an initialization batch).  This assert is to identify if we've encountered such a
		// case, so we know we need to solve it.
		//
		// We only run the assert for resources that are on the first GPU, as certain callers (like GPU Lightmass) create single GPU resources,
		// and don't attempt to pass in a specific GPU context.  The goal of the assert is to catch unexpected use cases where something other
		// than the default command context is passed in, and it's good enough to catch that just on the first GPU, assuming any multi-GPU
		// client will be using resources on all GPUs at some point.
		if (Buffer->GetParentDevice()->GetGPUIndex() == 0)
		{
			check(&CommandContext == &Buffer->GetParentDevice()->GetDefaultCommandContext());
		}
#endif

		for (FD3D12Buffer::FLinkedObjectIterator CurrentBuffer(Buffer); CurrentBuffer; ++CurrentBuffer)
		{
			FD3D12Resource* Destination = CurrentBuffer->ResourceLocation.GetResource();
			FD3D12Device* Device = Destination->GetParentDevice();
#if WITH_MGPU
			FD3D12CommandContext& CurrentCommandContext = Device->GetDefaultCommandContext();
#else
			FD3D12CommandContext& CurrentCommandContext = CommandContext;
#endif

			FD3D12CommandListHandle& hCommandList = CurrentCommandContext.CommandListHandle;
			// Copy from the temporary upload heap to the default resource
			{
				// if resource doesn't require state tracking then transition to copy dest here (could have been suballocated from shared resource) - not very optimal and should be batched
				if (!Destination->RequiresResourceStateTracking())
				{
					hCommandList.AddTransitionBarrier(Destination, Destination->GetDefaultResourceState(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
				}

				CurrentCommandContext.numInitialResourceCopies++;
				hCommandList.FlushResourceBarriers();
				hCommandList->CopyBufferRegion(
					Destination->GetResource(),
					CurrentBuffer->ResourceLocation.GetOffsetFromBaseOfResource(),
					SrcResourceLoc.GetResource()->GetResource(),
					SrcResourceLoc.GetOffsetFromBaseOfResource(), Size);

				// Update the resource state after the copy has been done (will take care of updating the residency as well)
				if (DestinationState != D3D12_RESOURCE_STATE_COPY_DEST)
				{
					hCommandList.AddTransitionBarrier(Destination, D3D12_RESOURCE_STATE_COPY_DEST, DestinationState, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
				}

				if (Destination->RequiresResourceStateTracking())
				{
					// Update the tracked resource state of this resource in the command list
					CResourceState& ResourceState = hCommandList.GetResourceState(Destination);
					ResourceState.SetResourceState(DestinationState);
					Destination->GetResourceState().SetResourceState(DestinationState);

					// Add dummy pending barrier, because the end state needs to be updated after execture command list with tracked state in the command list
					hCommandList.AddPendingResourceBarrier(Destination, D3D12_RESOURCE_STATE_TBD, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
				}
				else
				{
					check(Destination->GetDefaultResourceState() == DestinationState);
				}

				hCommandList.UpdateResidency(SrcResourceLoc.GetResource());

				CurrentCommandContext.ConditionalFlushCommandList();
			}

			// Buffer is now written and ready, so unlock the block (locked after creation and can be defragmented if needed)
			CurrentBuffer->ResourceLocation.UnlockPoolData();
		}
	}
};


void FD3D12Buffer::UploadResourceData(FRHICommandListBase& RHICmdList, FResourceArrayInterface* InResourceArray, D3D12_RESOURCE_STATES InDestinationState)
{
	check(InResourceArray);
	check(ResourceLocation.IsValid());

	uint32 BufferSize = GetSize();
	check(BufferSize == InResourceArray->GetResourceDataSize());

	if (EnumHasAnyFlags(GetUsage(), BUF_AnyDynamic))
	{
		// Copy directly in mapped data
		void* MappedUploadData = ResourceLocation.GetMappedBaseAddress(); 
		FMemory::Memcpy(MappedUploadData, InResourceArray->GetResourceData(), BufferSize);
	}
	else
	{
		const bool bOnAsyncThread = !IsInRHIThread() && !IsInRenderingThread();

		// Get an upload heap and initialize data
		FD3D12ResourceLocation SrcResourceLoc(GetParentDevice());
		void* pData;
		if (bOnAsyncThread)
		{
			const uint32 GPUIdx = SrcResourceLoc.GetParentDevice()->GetGPUIndex();
			pData = GetParentDevice()->GetParentAdapter()->GetUploadHeapAllocator(GPUIdx).AllocUploadResource(BufferSize, 4u, SrcResourceLoc);
		}
		else
		{
			pData = SrcResourceLoc.GetParentDevice()->GetDefaultFastAllocator().Allocate(BufferSize, 4UL, &SrcResourceLoc);
		}
		check(pData);
		FMemory::Memcpy(pData, InResourceArray->GetResourceData(), BufferSize);

		if (RHICmdList.IsBottomOfPipe())
		{
			// On RHIT or RT (when bypassing), we can access immediate context directly
			FD3D12RHICommandInitializeBuffer Command(this, SrcResourceLoc, BufferSize, InDestinationState);
			FD3D12CommandContext& CommandContext = GetParentDevice()->GetDefaultCommandContext();
			Command.ExecuteOnCommandContext(CommandContext);
		}
		else
		{
			new (RHICmdList.AllocCommand<FD3D12RHICommandInitializeBuffer>()) FD3D12RHICommandInitializeBuffer(this, SrcResourceLoc, BufferSize, InDestinationState);
		}
	}

	// Discard the resource array's contents.
	InResourceArray->Discard();
}


FD3D12SyncPoint FD3D12Buffer::UploadResourceDataViaCopyQueue(FResourceArrayInterface* InResourceArray)
{
	// assume not dynamic and not on async thread (probably fine but untested)
	check(IsInRHIThread() || IsInRenderingThread());
	check(!(GetUsage() & BUF_AnyDynamic));

	uint32 BufferSize = GetSize();

	// Get an upload heap and copy the data
	FD3D12ResourceLocation SrcResourceLoc(GetParentDevice());
	void* pData = GetParentDevice()->GetDefaultFastAllocator().Allocate(BufferSize, 4UL, &SrcResourceLoc);
	check(pData);
	FMemory::Memcpy(pData, InResourceArray->GetResourceData(), BufferSize);

	// Allocate copy queue command list and perform the copy op
	FD3D12Device* Device = SrcResourceLoc.GetParentDevice();
	FD3D12CommandAllocatorManager& CommandAllocatorManager = Device->GetTextureStreamingCommandAllocatorManager();
	FD3D12CommandAllocator* CurrentCommandAllocator = CommandAllocatorManager.ObtainCommandAllocator();
	FD3D12CommandListHandle hCopyCommandList = Device->GetCopyCommandListManager().ObtainCommandList(*CurrentCommandAllocator);

	// Required for stat tracking ?!?
	hCopyCommandList.SetCurrentOwningContext(&Device->GetDefaultCommandContext());
	hCopyCommandList.GetCurrentOwningContext()->numCopies++;

	// Perform actual copy op		
	hCopyCommandList->CopyBufferRegion(
		ResourceLocation.GetResource()->GetResource(),
		ResourceLocation.GetOffsetFromBaseOfResource(),
		SrcResourceLoc.GetResource()->GetResource(),
		SrcResourceLoc.GetOffsetFromBaseOfResource(), BufferSize);

	// Residency update needed since it's just been created?
	hCopyCommandList.UpdateResidency(ResourceLocation.GetResource());

	// Close and kick the command list without waiting for it
	hCopyCommandList.Close();
	bool bWaitForCompletion = false;

	FD3D12SyncPoint CopyQueueSyncPoint;
	GetID3D12DynamicRHI()->RHIExecuteOnCopyCommandQueue([Device, &hCopyCommandList, bWaitForCompletion, &CopyQueueSyncPoint](ID3D12CommandQueue* D3DCommandQueue) -> void
	{
		CopyQueueSyncPoint = Device->GetCopyCommandListManager().ExecuteCommandListNoCopyQueueSync(hCopyCommandList, bWaitForCompletion);
	});
	// Release command allocator (has the sync point as well)
	CommandAllocatorManager.ReleaseCommandAllocator(CurrentCommandAllocator);

	// Buffer is now written and ready, so unlock the block
	ResourceLocation.UnlockPoolData();

	// Discard the resource array's contents.
	InResourceArray->Discard();

	return CopyQueueSyncPoint;
}


void FD3D12Adapter::AllocateBuffer(FD3D12Device* Device,
	const D3D12_RESOURCE_DESC& InDesc,
	uint32 Size,
	EBufferUsageFlags InUsage,
	ED3D12ResourceStateMode InResourceStateMode,
	D3D12_RESOURCE_STATES InCreateState,
	uint32 Alignment,
	FD3D12Buffer* Buffer,
	FD3D12ResourceLocation& ResourceLocation,
	ID3D12ResourceAllocator* ResourceAllocator,
	const TCHAR* InDebugName)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(D3D12RHI::AllocateBuffer);

	// Explicitly check that the size is nonzero before allowing CreateBuffer to opaquely fail.
	check(Size > 0);

	if (EnumHasAnyFlags(InUsage, BUF_AnyDynamic))
	{
		check(ResourceAllocator == nullptr);
		check(InResourceStateMode != ED3D12ResourceStateMode::MultiState);
		check(InCreateState == D3D12_RESOURCE_STATE_GENERIC_READ);
		GetUploadHeapAllocator(Device->GetGPUIndex()).AllocUploadResource(Size, Alignment, ResourceLocation);
		check(ResourceLocation.GetSize() == Size);
	}
	else
	{
		if (ResourceAllocator)
		{
			ResourceAllocator->AllocateResource(Device->GetGPUIndex(), D3D12_HEAP_TYPE_DEFAULT, InDesc, InDesc.Width, Alignment, InResourceStateMode, InCreateState, nullptr, InDebugName, ResourceLocation);
		}
		else
		{
			Device->GetDefaultBufferAllocator().AllocDefaultResource(D3D12_HEAP_TYPE_DEFAULT, InDesc, InUsage, InResourceStateMode, InCreateState, ResourceLocation, Alignment, InDebugName);
		}
		ResourceLocation.SetOwner(Buffer);
		check(ResourceLocation.GetSize() == Size);
	}
}

FD3D12Buffer* FD3D12Adapter::CreateRHIBuffer(
	const D3D12_RESOURCE_DESC& InDesc,
	uint32 Alignment,
	uint32 Stride,
	uint32 Size,
	EBufferUsageFlags InUsage,
	ED3D12ResourceStateMode InResourceStateMode,
	D3D12_RESOURCE_STATES InCreateState,
	bool bHasInitialData,
	const FRHIGPUMask& InGPUMask,
	ID3D12ResourceAllocator* ResourceAllocator,
	const TCHAR* InDebugName)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(D3D12RHI::CreateRHIBuffer);
	SCOPE_CYCLE_COUNTER(STAT_D3D12CreateBufferTime);

	check(InDesc.Width == Size);

	FD3D12Buffer* BufferOut = nullptr;

	if (EnumHasAnyFlags(InUsage, BUF_AnyDynamic))
	{
		const uint32 FirstGPUIndex = InGPUMask.GetFirstIndex();

		FD3D12Buffer* NewBuffer0 = nullptr;
		BufferOut = CreateLinkedObject<FD3D12Buffer>(InGPUMask, [&](FD3D12Device* Device)
		{
			FD3D12Buffer* NewBuffer = new FD3D12Buffer(Device, Size, InUsage, Stride);
			NewBuffer->BufferAlignment = Alignment;

#if NAME_OBJECTS
			if (InDebugName)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(D3D12RHI::SetDebugName);
				NewBuffer->SetName(InDebugName);
			}
#endif // NAME_OBJECTS

			if ((Device->GetGPUIndex() == FirstGPUIndex) || EnumHasAnyFlags(InUsage, BUF_MultiGPUAllocate))
			{
				AllocateBuffer(Device, InDesc, Size, InUsage, InResourceStateMode, InCreateState, Alignment, NewBuffer, NewBuffer->ResourceLocation, ResourceAllocator, InDebugName);
				NewBuffer0 = NewBuffer;
			}
			else
			{
				check(NewBuffer0);
				FD3D12ResourceLocation::ReferenceNode(Device, NewBuffer->ResourceLocation, NewBuffer0->ResourceLocation);
			}

			return NewBuffer;
		});
	}
	else
	{
		BufferOut = CreateLinkedObject<FD3D12Buffer>(InGPUMask, [&](FD3D12Device* Device)
		{
			FD3D12Buffer* NewBuffer = new FD3D12Buffer(Device, Size, InUsage, Stride);
			NewBuffer->BufferAlignment = Alignment;

#if NAME_OBJECTS
			if (InDebugName)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(D3D12RHI::SetDebugName);
				NewBuffer->SetName(InDebugName);
			}
#endif // NAME_OBJECTS

			AllocateBuffer(Device, InDesc, Size, InUsage, InResourceStateMode, InCreateState, Alignment, NewBuffer, NewBuffer->ResourceLocation, ResourceAllocator, InDebugName);
			
			// Unlock immediately if there is no initial data
			if (!bHasInitialData)
			{
				NewBuffer->ResourceLocation.UnlockPoolData();
			}

			return NewBuffer;
		});
	}

	// Don't track transient buffer stats here
	if (!BufferOut->ResourceLocation.IsTransient())
	{
		UpdateBufferStats((EBufferUsageFlags)InUsage, BufferOut->ResourceLocation.GetSize());
	}

	return BufferOut;
}

void FD3D12Buffer::Rename(FD3D12ResourceLocation& NewLocation)
{
	FD3D12ResourceLocation::TransferOwnership(ResourceLocation, NewLocation);
	ResourceRenamed(&ResourceLocation);
}

void FD3D12Buffer::RenameLDAChain(FD3D12ResourceLocation& NewLocation)
{
	// Dynamic buffers use cross-node resources (with the exception of BUF_MultiGPUAllocate)
	//ensure(GetUsage() & BUF_AnyDynamic);
	Rename(NewLocation);

	if (GNumExplicitGPUsForRendering > 1)
	{
		ensure(GetParentDevice() == NewLocation.GetParentDevice());

		if (EnumHasAnyFlags(GetUsage(), BUF_MultiGPUAllocate) == false)
		{
			ensure(IsHeadLink());

			// Update all of the resources in the LDA chain to reference this cross-node resource
			for (auto NextBuffer = ++FLinkedObjectIterator(this); NextBuffer; ++NextBuffer)
			{
				FD3D12ResourceLocation::ReferenceNode(NextBuffer->GetParentDevice(), NextBuffer->ResourceLocation, ResourceLocation);
				NextBuffer->ResourceRenamed(&NextBuffer->ResourceLocation);
			}
		}
	}
}

void FD3D12Buffer::Swap(FD3D12Buffer& Other)
{
	check(!LockedData.bLocked && !Other.LockedData.bLocked);

	FRHIBuffer::Swap(Other);
	FD3D12BaseShaderResource::Swap(Other);
	FD3D12LinkedAdapterObject<FD3D12Buffer>::Swap(Other);

	check(IsHeadLink());
	for (FLinkedObjectIterator NextBuffer(this); NextBuffer; ++NextBuffer)
	{
		NextBuffer->ResourceRenamed(&NextBuffer->ResourceLocation);
	}
}

void FD3D12Buffer::ReleaseUnderlyingResource()
{
	int64 BufferSize = ResourceLocation.GetSize();
	bool bTransient = ResourceLocation.IsTransient();
	if (!bTransient)
	{
		UpdateBufferStats((EBufferUsageFlags)GetUsage(), -BufferSize);
	}

	check(IsHeadLink());
	for (FLinkedObjectIterator NextBuffer(this); NextBuffer; ++NextBuffer)
	{
		check(!NextBuffer->LockedData.bLocked && NextBuffer->ResourceLocation.IsValid());
		NextBuffer->ResourceLocation.Clear();
		NextBuffer->ResourceRenamed(&NextBuffer->ResourceLocation);
	}
}

void FD3D12Buffer::GetResourceDescAndAlignment(uint64 InSize, uint32 InStride, EBufferUsageFlags& InUsage, D3D12_RESOURCE_DESC& ResourceDesc, uint32& Alignment)
{
	ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(InSize);

	if (EnumHasAnyFlags(InUsage, BUF_UnorderedAccess))
	{
		ResourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		static bool bRequiresRawView = (GMaxRHIFeatureLevel < ERHIFeatureLevel::SM5);
		if (bRequiresRawView)
		{
			// Force the buffer to be a raw, byte address buffer
			InUsage |= BUF_ByteAddressBuffer;
		}
	}

	if (!EnumHasAnyFlags(InUsage, BUF_ShaderResource))
	{
		ResourceDesc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
	}

	if (EnumHasAnyFlags(InUsage, BUF_DrawIndirect))
	{
		ResourceDesc.Flags |= D3D12RHI_RESOURCE_FLAG_ALLOW_INDIRECT_BUFFER;
	}

	// Structured buffers, non-ByteAddress buffers, need to be aligned to their stride to ensure that they can be addressed correctly with element based offsets.
	Alignment = (InStride > 0) && (EnumHasAnyFlags(InUsage, BUF_StructuredBuffer) || !EnumHasAnyFlags(InUsage, BUF_ByteAddressBuffer | BUF_DrawIndirect)) ? InStride : 4;
}

FBufferRHIRef FD3D12DynamicRHI::RHICreateBuffer(FRHICommandListBase& RHICmdList, uint32 Size, EBufferUsageFlags Usage, uint32 Stride, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	return CreateBuffer(RHICmdList, Size, Usage, Stride, InResourceState, CreateInfo);
}

FBufferRHIRef FD3D12DynamicRHI::CreateBuffer(FRHICommandListBase& RHICmdList, uint32 Size, EBufferUsageFlags Usage, uint32 Stride, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	if (CreateInfo.bWithoutNativeResource)
	{
		return GetAdapter().CreateLinkedObject<FD3D12Buffer>(CreateInfo.GPUMask, [](FD3D12Device* Device)
			{
				return new FD3D12Buffer();
			});
	}

	return CreateD3D12Buffer(&RHICmdList, Size, Usage, Stride, InResourceState, CreateInfo);
}

FD3D12Buffer* FD3D12DynamicRHI::CreateD3D12Buffer(class FRHICommandListBase* RHICmdList, uint32 Size, EBufferUsageFlags Usage, uint32 Stride, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo, ID3D12ResourceAllocator* ResourceAllocator)
{
	D3D12_RESOURCE_DESC Desc;
	uint32 Alignment;
	FD3D12Buffer::GetResourceDescAndAlignment(Size, Stride, Usage, Desc, Alignment);

	ED3D12ResourceStateMode StateMode = EnumHasAllFlags(Usage, BUF_AccelerationStructure) 
		? ED3D12ResourceStateMode::SingleState 
		: ED3D12ResourceStateMode::Default;

	const bool bIsDynamic = EnumHasAnyFlags(Usage, BUF_AnyDynamic);
	D3D12_HEAP_TYPE HeapType = bIsDynamic ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
	const FD3D12Resource::FD3D12ResourceTypeHelper Type(Desc, HeapType);

	// Does this resource support tracking?
	const bool bSupportResourceStateTracking = !bIsDynamic && FD3D12DefaultBufferAllocator::IsPlacedResource(Desc.Flags, StateMode, Alignment) && Type.bWritable;

	// Initial state is derived from the InResourceState if it supports tracking
	D3D12_RESOURCE_STATES DesiredState = bSupportResourceStateTracking ? Type.GetOptimalInitialState(InResourceState, false) :
		FD3D12DefaultBufferAllocator::GetDefaultInitialResourceState(HeapType, (EBufferUsageFlags)Usage, StateMode);

	// Setup the state at which the resource needs to be created - copy dest only supported for placed resources
	D3D12_RESOURCE_STATES CreateState = (CreateInfo.ResourceArray && bSupportResourceStateTracking) ? D3D12_RESOURCE_STATE_COPY_DEST : DesiredState;
	bool bHasInitialData = CreateInfo.ResourceArray != nullptr;

	FD3D12Buffer* Buffer = GetAdapter().CreateRHIBuffer(Desc, Alignment, Stride, Size, Usage, StateMode, CreateState, bHasInitialData, CreateInfo.GPUMask, ResourceAllocator, CreateInfo.DebugName);
	check(Buffer->ResourceLocation.IsValid());

	// Copy the resource data if available 
	if (bHasInitialData)
	{
		check(RHICmdList);
		Buffer->UploadResourceData(*RHICmdList, CreateInfo.ResourceArray, DesiredState);
	}

	return Buffer;
}

FRHIBuffer* FD3D12DynamicRHI::CreateBuffer(const FRHIBufferCreateInfo& CreateInfo, const TCHAR* DebugName, ERHIAccess InitialState, ID3D12ResourceAllocator* ResourceAllocator)
{
	FRHIResourceCreateInfo ResourceCreateInfo(DebugName);
	return CreateD3D12Buffer(nullptr, CreateInfo.Size, CreateInfo.Usage, CreateInfo.Stride, InitialState, ResourceCreateInfo, ResourceAllocator);
}

void* FD3D12DynamicRHI::LockBuffer(FRHICommandListBase& RHICmdList, FD3D12Buffer* Buffer, uint32 BufferSize, EBufferUsageFlags BufferUsage, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	SCOPE_CYCLE_COUNTER(STAT_D3D12LockBufferTime);

	check(Size <= BufferSize);

	FD3D12LockedResource& LockedData = Buffer->LockedData;
	check(LockedData.bLocked == false);
	FD3D12Adapter& Adapter = GetAdapter();

	void* Data = nullptr;

	// Determine whether the buffer is dynamic or not.
	if (EnumHasAnyFlags(BufferUsage, BUF_AnyDynamic))
	{
		check(LockMode == RLM_WriteOnly || LockMode == RLM_WriteOnly_NoOverwrite);

		if (LockedData.bHasNeverBeenLocked)
		{
			// Buffers on upload heap are mapped right after creation
			Data = Buffer->ResourceLocation.GetMappedBaseAddress();
			check(!!Data);
		}
		else
		{
			FD3D12Device* Device = Buffer->GetParentDevice();

			// If on the RenderThread, queue up a command on the RHIThread to rename this buffer at the correct time
			if (RHICmdList.IsTopOfPipe() && LockMode == RLM_WriteOnly)
			{
				FRHICommandRenameUploadBuffer* Command = ALLOC_COMMAND_CL(RHICmdList, FRHICommandRenameUploadBuffer)(Buffer, Device);
				Data = Adapter.GetUploadHeapAllocator(Device->GetGPUIndex()).AllocUploadResource(BufferSize, Buffer->BufferAlignment, Command->NewLocation);
				RHICmdList.RHIThreadFence(true);
			}
			else
			{
				FRHICommandRenameUploadBuffer Command(Buffer, Device);
				Data = Adapter.GetUploadHeapAllocator(Device->GetGPUIndex()).AllocUploadResource(BufferSize, Buffer->BufferAlignment, Command.NewLocation);
				Command.Execute(RHICmdList);
			}
		}
	}
	else
	{
		// Static and read only buffers only have one version of the content. Use the first related device.
		FD3D12Device* Device = Buffer->GetParentDevice();
		FD3D12Resource* pResource = Buffer->ResourceLocation.GetResource();

		// Locking for read must occur immediately so we can't queue up the operations later.
		if (LockMode == RLM_ReadOnly)
		{
			FRHICommandListImmediate& RHICmdListImmediate = RHICmdList.GetAsImmediate();

			LockedData.bLockedForReadOnly = true;
			// If the static buffer is being locked for reading, create a staging buffer.
			FD3D12Resource* StagingBuffer = nullptr;

			const FRHIGPUMask Node = Device->GetGPUMask();
			VERIFYD3D12RESULT(Adapter.CreateBuffer(D3D12_HEAP_TYPE_READBACK, Node, Node, Offset + Size, &StagingBuffer, nullptr));

			// Copy the contents of the buffer to the staging buffer.
			{
				const auto& pfnCopyContents = [&]()
				{
					FD3D12CommandContext& DefaultContext = Device->GetDefaultCommandContext();

					FD3D12CommandListHandle& hCommandList = DefaultContext.CommandListHandle;
					FScopedResourceBarrier ScopeResourceBarrierSource(hCommandList, pResource, D3D12_RESOURCE_STATE_COPY_SOURCE, 0, FD3D12DynamicRHI::ETransitionMode::Apply);
					// Don't need to transition upload heaps

					uint64 SubAllocOffset = Buffer->ResourceLocation.GetOffsetFromBaseOfResource();

					DefaultContext.numCopies++;
					hCommandList.FlushResourceBarriers();	// Must flush so the desired state is actually set.
					hCommandList->CopyBufferRegion(
						StagingBuffer->GetResource(),
						0,
						pResource->GetResource(),
						SubAllocOffset + Offset, Size);

					hCommandList.UpdateResidency(StagingBuffer);
					hCommandList.UpdateResidency(pResource);

					DefaultContext.FlushCommands(true);
				};

				if (RHICmdListImmediate.IsTopOfPipe())
				{
					// Sync when in the render thread implementation
					check(IsInRHIThread() == false);

					RHICmdListImmediate.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
					pfnCopyContents();
				}
				else
				{
					check(IsInRenderingThread() && !IsRHIThreadRunning());
					pfnCopyContents();
				}
			}

			LockedData.ResourceLocation.AsStandAlone(StagingBuffer, Size);
			Data = LockedData.ResourceLocation.GetMappedBaseAddress();
		}
		else
		{
			// If the static buffer is being locked for writing, allocate memory for the contents to be written to.
			Data = Device->GetDefaultFastAllocator().Allocate(Size, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, &LockedData.ResourceLocation);
		}
	}

	LockedData.LockedOffset = Offset;
	LockedData.LockedPitch = Size;
	LockedData.bLocked = true;
	LockedData.bHasNeverBeenLocked = false;

	// Return the offset pointer
	check(Data != nullptr);
	return Data;
}

void FD3D12DynamicRHI::UnlockBuffer(FRHICommandListBase& RHICmdList, FD3D12Buffer* Buffer, EBufferUsageFlags BufferUsage)
{
	SCOPE_CYCLE_COUNTER(STAT_D3D12UnlockBufferTime);

	FD3D12LockedResource& LockedData = Buffer->LockedData;
	check(LockedData.bLocked == true);

	// Determine whether the buffer is dynamic or not.
	if (EnumHasAnyFlags(BufferUsage, BUF_AnyDynamic))
	{
		// If the Buffer is dynamic, its upload heap memory can always stay mapped. Don't do anything.
	}
	else
	{
		if (LockedData.bLockedForReadOnly)
		{
			//Nothing to do, just release the locked data at the end of the function
		}
		else
		{
			// Update all of the resources in the LDA chain
			check(Buffer->IsHeadLink());
			FD3D12Buffer* LastBuffer = Buffer->GetLinkedObject(Buffer->GetLinkedObjectsGPUMask().GetLastIndex());

			for (FD3D12Buffer::FLinkedObjectIterator CurrentBuffer(Buffer); CurrentBuffer; ++CurrentBuffer)
			{
				// If we are on the render thread, queue up the copy on the RHIThread so it happens at the correct time.
				if (RHICmdList.IsTopOfPipe())
				{
					if (CurrentBuffer.Get() == LastBuffer)
					{
						// Command associated with last buffer (will be only buffer if single GPU) receives ownership of locked data
						ALLOC_COMMAND_CL(RHICmdList, FRHICommandUpdateBuffer)(&CurrentBuffer->ResourceLocation, LockedData.ResourceLocation, LockedData.LockedOffset, LockedData.LockedPitch);
					}
					else
					{
						// Other commands receive a reference copy of the locked data.  Commands get replayed in order, with the
						// last command handling clean up the locked data after it has been propagated to all GPUs.
						FD3D12ResourceLocation NodeResourceLocation(LockedData.ResourceLocation.GetParentDevice());
						FD3D12ResourceLocation::ReferenceNode(NodeResourceLocation.GetParentDevice(), NodeResourceLocation, LockedData.ResourceLocation);
						ALLOC_COMMAND_CL(RHICmdList, FRHICommandUpdateBuffer)(&CurrentBuffer->ResourceLocation, NodeResourceLocation, LockedData.LockedOffset, LockedData.LockedPitch);
					}
				}
				else
				{
					UpdateBuffer(&CurrentBuffer->ResourceLocation, LockedData.LockedOffset, &LockedData.ResourceLocation, 0, LockedData.LockedPitch);
				}
			}
		}
	}

	LockedData.Reset();
}

void* FD3D12DynamicRHI::RHILockBuffer(FRHICommandListBase& RHICmdList, FRHIBuffer* BufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	// If you hit this assert, you should be using LockBufferMGPU and iterating over FRHIGPUMask::All() to initialize the resource separately for each GPU.
	// "MultiGPUAllocate" only makes sense if a buffer must vary per GPU, for example if it's a buffer that includes GPU specific virtual addresses for ray
	// tracing acceleration structures.
	check(!EnumHasAnyFlags(BufferRHI->GetUsage(), BUF_MultiGPUAllocate));

	FD3D12Buffer* Buffer = FD3D12DynamicRHI::ResourceCast(BufferRHI);
	return LockBuffer(RHICmdList, Buffer, Buffer->GetSize(), Buffer->GetUsage(), Offset, Size, LockMode);
}

void* FD3D12DynamicRHI::RHILockBufferMGPU(FRHICommandListBase& RHICmdList, FRHIBuffer* BufferRHI, uint32 GPUIndex, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	// If you hit this assert, you should be using LockBuffer to initialize the resource, rather than this function.  The MGPU version is only for resources
	// with the MultiGPUAllocate flag, where it's necessary for the caller to initialize the buffer for each GPU.  The other LockBuffer call initializes the
	// resource on all GPUs with one call, due to driver mirroring of the underlying resource.
	check(EnumHasAnyFlags(BufferRHI->GetUsage(), BUF_MultiGPUAllocate));

	FD3D12Buffer* Buffer = FD3D12DynamicRHI::ResourceCast(BufferRHI, GPUIndex);
	return LockBuffer(RHICmdList, Buffer, Buffer->GetSize(), Buffer->GetUsage(), Offset, Size, LockMode);
}

void FD3D12DynamicRHI::RHIUnlockBuffer(FRHICommandListBase& RHICmdList, FRHIBuffer* BufferRHI)
{
	check(!EnumHasAnyFlags(BufferRHI->GetUsage(), BUF_MultiGPUAllocate));

	FD3D12Buffer* Buffer = FD3D12DynamicRHI::ResourceCast(BufferRHI);
	UnlockBuffer(RHICmdList, Buffer, Buffer->GetUsage());
}

void FD3D12DynamicRHI::RHIUnlockBufferMGPU(FRHICommandListBase& RHICmdList, FRHIBuffer* BufferRHI, uint32 GPUIndex)
{
	check(EnumHasAnyFlags(BufferRHI->GetUsage(), BUF_MultiGPUAllocate));

	FD3D12Buffer* Buffer = FD3D12DynamicRHI::ResourceCast(BufferRHI, GPUIndex);
	UnlockBuffer(RHICmdList, Buffer, Buffer->GetUsage());
}

void FD3D12DynamicRHI::RHITransferBufferUnderlyingResource(FRHIBuffer* DestBuffer, FRHIBuffer* SrcBuffer)
{
	check(DestBuffer);
	FD3D12Buffer* Dest = ResourceCast(DestBuffer);
	if (!SrcBuffer)
	{
		Dest->ReleaseUnderlyingResource();
	}
	else
	{
		FD3D12Buffer* Src = ResourceCast(SrcBuffer);
		Dest->Swap(*Src);
	}
}

void FD3D12DynamicRHI::RHICopyBuffer(FRHIBuffer* SourceBufferRHI, FRHIBuffer* DestBufferRHI)
{
	FD3D12Buffer* SrcBuffer = FD3D12DynamicRHI::ResourceCast(SourceBufferRHI);
	FD3D12Buffer* DstBuffer = FD3D12DynamicRHI::ResourceCast(DestBufferRHI);
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

		check(SourceBuffer->GetSize() == DestBuffer->GetSize());

		FD3D12CommandContext& Context = Device->GetDefaultCommandContext();
		Context.numCopies++;

		// The underlying D3D12 buffer can be larger than the RHI buffer due to pooling.
		Context.CommandListHandle->CopyBufferRegion(
			pDestResource->GetResource(), 
			DestBuffer->ResourceLocation.GetOffsetFromBaseOfResource(), 
			pSourceResource->GetResource(), 
			SourceBuffer->ResourceLocation.GetOffsetFromBaseOfResource(), 
			SourceBufferRHI->GetSize());

		Context.CommandListHandle.UpdateResidency(pDestResource);
		Context.CommandListHandle.UpdateResidency(pSourceResource);

		Context.ConditionalFlushCommandList();

		DEBUG_EXECUTE_COMMAND_CONTEXT(Device->GetDefaultCommandContext());

		Device->RegisterGPUWork(1);
	}
}

void FD3D12DynamicRHI::RHIBindDebugLabelName(FRHIBuffer* BufferRHI, const TCHAR* Name)
{
#if NAME_OBJECTS
	FD3D12Buffer* Buffer = FD3D12DynamicRHI::ResourceCast(BufferRHI);

	FD3D12Buffer::FLinkedObjectIterator BufferIt(Buffer);

	if (GNumExplicitGPUsForRendering > 1)
	{
		// Generate string of the form "Name (GPU #)" -- assumes GPU index is a single digit.  This is called many times
		// a frame, so we want to avoid any string functions which dynamically allocate, to reduce perf overhead.
		static_assert(MAX_NUM_GPUS <= 10);

		static const TCHAR NameSuffix[] = TEXT(" (GPU #)");
		constexpr int32 NameSuffixLengthWithTerminator = (int32)UE_ARRAY_COUNT(NameSuffix);
		constexpr int32 NameBufferLength = 256;
		constexpr int32 GPUIndexSuffixOffset = 6;		// Offset of '#' character

		// Combine Name and suffix in our string buffer (clamping the length for bounds checking).  We'll replace the GPU index
		// with the appropriate digit in the loop.
		int32 NameLength = FMath::Min(FCString::Strlen(Name), NameBufferLength - NameSuffixLengthWithTerminator);
		int32 GPUIndexOffset = NameLength + GPUIndexSuffixOffset;

		TCHAR DebugName[NameBufferLength];
		FMemory::Memcpy(&DebugName[0], Name, NameLength*sizeof(TCHAR));
		FMemory::Memcpy(&DebugName[NameLength], NameSuffix, NameSuffixLengthWithTerminator*sizeof(TCHAR));

		for (; BufferIt; ++BufferIt)
		{
			FD3D12Resource* Resource = BufferIt->GetResource();

			DebugName[GPUIndexOffset] = TEXT('0') + BufferIt->GetParentDevice()->GetGPUIndex();

			SetName(Resource, DebugName);
		}
	}
	else
	{
		SetName(Buffer->GetResource(), Name);
	}
#endif

	// Also set on RHI object
	BufferRHI->SetName(Name);
}

void FD3D12CommandContext::RHICopyBufferRegion(FRHIBuffer* DestBufferRHI, uint64 DstOffset, FRHIBuffer* SourceBufferRHI, uint64 SrcOffset, uint64 NumBytes)
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

	FScopedResourceBarrier ScopeResourceBarrierSource(CommandListHandle, pSourceResource, D3D12_RESOURCE_STATE_COPY_SOURCE, 0, FD3D12DynamicRHI::ETransitionMode::Validate);
	FScopedResourceBarrier ScopeResourceBarrierDest(CommandListHandle, pDestResource, D3D12_RESOURCE_STATE_COPY_DEST, 0, FD3D12DynamicRHI::ETransitionMode::Validate);
	CommandListHandle.FlushResourceBarriers();

	CommandListHandle->CopyBufferRegion(pDestResource->GetResource(), DestBuffer->ResourceLocation.GetOffsetFromBaseOfResource() + DstOffset, pSourceResource->GetResource(), SourceBuffer->ResourceLocation.GetOffsetFromBaseOfResource() + SrcOffset, NumBytes);
	CommandListHandle.UpdateResidency(pDestResource);
	CommandListHandle.UpdateResidency(pSourceResource);

	ConditionalFlushCommandList();

	Device->RegisterGPUWork(1);
}
