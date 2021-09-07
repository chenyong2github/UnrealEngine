// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
D3D12Buffer.cpp: D3D Common code for buffers.
=============================================================================*/

#include "D3D12RHIPrivate.h"

// Forward declarations are required for the template functions
template FD3D12VertexBuffer* FD3D12Adapter::CreateRHIBuffer<FD3D12VertexBuffer>(FRHICommandListImmediate* RHICmdList,
	const D3D12_RESOURCE_DESC& Desc,
	uint32 Alignment, uint32 Stride, uint32 Size, uint32 InUsage,
	ED3D12ResourceStateMode InResourceStateMode,
	FRHIResourceCreateInfo& CreateInfo);

template FD3D12IndexBuffer* FD3D12Adapter::CreateRHIBuffer<FD3D12IndexBuffer>(FRHICommandListImmediate* RHICmdList,
	const D3D12_RESOURCE_DESC& Desc,
	uint32 Alignment, uint32 Stride, uint32 Size, uint32 InUsage,
	ED3D12ResourceStateMode InResourceStateMode,
	FRHIResourceCreateInfo& CreateInfo);

template FD3D12StructuredBuffer* FD3D12Adapter::CreateRHIBuffer<FD3D12StructuredBuffer>(FRHICommandListImmediate* RHICmdList,
	const D3D12_RESOURCE_DESC& Desc,
	uint32 Alignment, uint32 Stride, uint32 Size, uint32 InUsage,
	ED3D12ResourceStateMode InResourceStateMode,
	FRHIResourceCreateInfo& CreateInfo);

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
		FD3D12DynamicRHI::GetD3DRHI()->UpdateBuffer(Destination->GetResource(), Destination->GetOffsetFromBaseOfResource() + DestinationOffset, Source.GetResource(), Source.GetOffsetFromBaseOfResource(), NumBytes);
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
		Resource->RenameLDAChain(NewLocation);
	}
};

struct FD3D12RHICommandInitializeBufferString
{
	static const TCHAR* TStr() { return TEXT("FD3D12RHICommandInitializeBuffer"); }
};
struct FD3D12RHICommandInitializeBuffer final : public FRHICommand<FD3D12RHICommandInitializeBuffer, FD3D12RHICommandInitializeBufferString>
{
	FD3D12Buffer* Buffer;
	FD3D12ResourceLocation SrcResourceLoc;
	uint32 Size;

	FORCEINLINE_DEBUGGABLE FD3D12RHICommandInitializeBuffer(FD3D12Buffer* InBuffer, FD3D12ResourceLocation& InSrcResourceLoc, uint32 InSize)
		: Buffer(InBuffer)
		, SrcResourceLoc(InSrcResourceLoc.GetParentDevice())
		, Size(InSize)
	{
		FD3D12ResourceLocation::TransferOwnership(SrcResourceLoc, InSrcResourceLoc);
	}

	void Execute(FRHICommandListBase& /* unused */)
	{
		ExecuteNoCmdList();
	}

	void ExecuteNoCmdList()
	{
		for (FD3D12Buffer::FLinkedObjectIterator CurrentBuffer(Buffer); CurrentBuffer; ++CurrentBuffer)
		{
			FD3D12Resource* Destination = CurrentBuffer->ResourceLocation.GetResource();
			FD3D12Device* Device = Destination->GetParentDevice();

			FD3D12CommandContext& CommandContext = Device->GetDefaultCommandContext();
			FD3D12CommandListHandle& hCommandList = CommandContext.CommandListHandle;
			// Copy from the temporary upload heap to the default resource
			{
				// Writable structured buffers are sometimes initialized with initial data which means they sometimes need tracking.
				FConditionalScopeResourceBarrier ConditionalScopeResourceBarrier(hCommandList, Destination, D3D12_RESOURCE_STATE_COPY_DEST, 0);

				CommandContext.numInitialResourceCopies++;
				hCommandList.FlushResourceBarriers();
				hCommandList->CopyBufferRegion(
					Destination->GetResource(),
					CurrentBuffer->ResourceLocation.GetOffsetFromBaseOfResource(),
					SrcResourceLoc.GetResource()->GetResource(),
					SrcResourceLoc.GetOffsetFromBaseOfResource(), Size);

				hCommandList.UpdateResidency(Destination);
				hCommandList.UpdateResidency(SrcResourceLoc.GetResource());

				CommandContext.ConditionalFlushCommandList();
			}
		}
	}
};

void FD3D12Adapter::AllocateBuffer(FD3D12Device* Device,
	const D3D12_RESOURCE_DESC& InDesc,
	uint32 Size,
	uint32 InUsage,
	ED3D12ResourceStateMode InResourceStateMode,
	FRHIResourceCreateInfo& CreateInfo,
	uint32 Alignment,
	FD3D12TransientResource& TransientResource,
	FD3D12ResourceLocation& ResourceLocation)
{
	// Explicitly check that the size is nonzero before allowing CreateBuffer to opaquely fail.
	check(Size > 0);

	const bool bIsDynamic = (InUsage & BUF_AnyDynamic) ? true : false;

	if (bIsDynamic)
	{
		check(InResourceStateMode != ED3D12ResourceStateMode::MultiState);
		void* pData = GetUploadHeapAllocator(Device->GetGPUIndex()).AllocUploadResource(Size, Alignment, ResourceLocation);
		check(ResourceLocation.GetSize() == Size);

		if (CreateInfo.ResourceArray)
		{
			const void* InitialData = CreateInfo.ResourceArray->GetResourceData();

			check(Size == CreateInfo.ResourceArray->GetResourceDataSize());
			// Handle initial data
			FMemory::Memcpy(pData, InitialData, Size);
		}
	}
	else
	{
		Device->GetDefaultBufferAllocator().AllocDefaultResource(D3D12_HEAP_TYPE_DEFAULT, InDesc, (EBufferUsageFlags)InUsage, InResourceStateMode, ResourceLocation, Alignment, CreateInfo.DebugName);
		check(ResourceLocation.GetSize() == Size);
	}
}

// This is a templated function used to create FD3D12VertexBuffers, FD3D12IndexBuffers and FD3D12StructuredBuffers
template<class BufferType>
BufferType* FD3D12Adapter::CreateRHIBuffer(FRHICommandListImmediate* RHICmdList,
	const D3D12_RESOURCE_DESC& InDesc,
	uint32 Alignment,
	uint32 Stride,
	uint32 Size,
	uint32 InUsage,
	ED3D12ResourceStateMode InResourceStateMode,
	FRHIResourceCreateInfo& CreateInfo)
{
	SCOPE_CYCLE_COUNTER(STAT_D3D12CreateBufferTime);

	const bool bIsDynamic = (InUsage & BUF_AnyDynamic) ? true : false;
	const uint32 FirstGPUIndex = CreateInfo.GPUMask.GetFirstIndex();

	BufferType* BufferOut = nullptr;
	if (bIsDynamic)
	{
		BufferType* NewBuffer0 = nullptr;
		BufferOut = CreateLinkedObject<BufferType>(CreateInfo.GPUMask, [&](FD3D12Device* Device)
		{
			BufferType* NewBuffer = new BufferType(Device, Stride, Size, InUsage);
			NewBuffer->BufferAlignment = Alignment;

			if (Device->GetGPUIndex() == FirstGPUIndex)
			{
				AllocateBuffer(Device, InDesc, Size, InUsage, InResourceStateMode, CreateInfo, Alignment, *NewBuffer, NewBuffer->ResourceLocation);
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
		BufferOut = CreateLinkedObject<BufferType>(CreateInfo.GPUMask, [&](FD3D12Device* Device)
		{
			BufferType* NewBuffer = new BufferType(Device, Stride, Size, InUsage);
			NewBuffer->BufferAlignment = Alignment;

			AllocateBuffer(Device, InDesc, Size, InUsage, InResourceStateMode, CreateInfo, Alignment, *NewBuffer, NewBuffer->ResourceLocation);

			return NewBuffer;
		});
	}

	if (CreateInfo.ResourceArray)
	{
		if (bIsDynamic == false && BufferOut->ResourceLocation.IsValid())
		{
			check(Size == CreateInfo.ResourceArray->GetResourceDataSize());

			const bool bOnAsyncThread = !IsInRHIThread() && !IsInRenderingThread();

			// Get an upload heap and initialize data
			FD3D12ResourceLocation SrcResourceLoc(BufferOut->GetParentDevice());
			void* pData;
			if (bOnAsyncThread)
			{
				const uint32 GPUIdx = SrcResourceLoc.GetParentDevice()->GetGPUIndex();
				pData = GetUploadHeapAllocator(GPUIdx).AllocUploadResource(Size, 4u, SrcResourceLoc);
			}
			else
			{
				pData = SrcResourceLoc.GetParentDevice()->GetDefaultFastAllocator().Allocate(Size, 4UL, &SrcResourceLoc);
			}
			check(pData);
			FMemory::Memcpy(pData, CreateInfo.ResourceArray->GetResourceData(), Size);
			
			if (bOnAsyncThread)
			{
				// Need to update buffer content on RHI thread (immediate context) because the buffer can be a
				// sub-allocation and its backing resource may be in a state incompatible with the copy queue.
				// TODO:
				// Create static buffers in COMMON state, rely on state promotion/decay to avoid transition barriers,
				// and initialize them asynchronously on the copy queue. D3D12 buffers always allow simultaneous acess
				// so it is legal to write to a region on the copy queue while other non-overlapping regions are
				// being read on the graphics/compute queue. Currently, d3ddebug throws error for such usage.
				// Once Microsoft (via Windows update) fix the debug layer, async static buffer initialization should
				// be done on the copy queue.
				FD3D12ResourceLocation* SrcResourceLoc_Heap = new FD3D12ResourceLocation(SrcResourceLoc.GetParentDevice());
				FD3D12ResourceLocation::TransferOwnership(*SrcResourceLoc_Heap, SrcResourceLoc);
				ENQUEUE_RENDER_COMMAND(CmdD3D12InitializeBuffer)(
					[BufferOut, SrcResourceLoc_Heap, Size](FRHICommandListImmediate& RHICmdList)
				{
					if (RHICmdList.Bypass())
					{
						FD3D12RHICommandInitializeBuffer Command(BufferOut, *SrcResourceLoc_Heap, Size);
						Command.ExecuteNoCmdList();
					}
					else
					{
						new (RHICmdList.AllocCommand<FD3D12RHICommandInitializeBuffer>()) FD3D12RHICommandInitializeBuffer(BufferOut, *SrcResourceLoc_Heap, Size);
					}
					delete SrcResourceLoc_Heap;
				});
			}
			else if (!RHICmdList || RHICmdList->Bypass())
			{
				// On RHIT or RT (when bypassing), we can access immediate context directly
				FD3D12RHICommandInitializeBuffer Command(BufferOut, SrcResourceLoc, Size);
				Command.ExecuteNoCmdList();
			}
			else
			{
				// On RT but not bypassing
				new (RHICmdList->AllocCommand<FD3D12RHICommandInitializeBuffer>()) FD3D12RHICommandInitializeBuffer(BufferOut, SrcResourceLoc, Size);
			}
		}

		// Discard the resource array's contents.
		CreateInfo.ResourceArray->Discard();
	}

	UpdateBufferStats<BufferType>(&BufferOut->ResourceLocation, true);

	return BufferOut;
}

void FD3D12Buffer::Rename(FD3D12ResourceLocation& NewLocation)
{
	FD3D12ResourceLocation::TransferOwnership(ResourceLocation, NewLocation);

	FScopeLock Lock(&DynamicSRVsCS);
	for (FD3D12BaseShaderResourceView* DynamicSRVBase : DynamicSRVs)
	{
		FD3D12ShaderResourceView* DynamicSRV = static_cast<FD3D12ShaderResourceView*>(DynamicSRVBase);
		DynamicSRV->Rename(ResourceLocation);
	}
}

void FD3D12Buffer::RenameLDAChain(FD3D12ResourceLocation& NewLocation)
{
	// Dynamic buffers use cross-node resources.
	//ensure(GetUsage() & BUF_AnyDynamic);
	Rename(NewLocation);

	if (GNumExplicitGPUsForRendering > 1)
	{
		// This currently crashes at exit time because NewLocation isn't tracked in the right allocator.
		ensure(IsHeadLink());
		ensure(GetParentDevice() == NewLocation.GetParentDevice());

		// Update all of the resources in the LDA chain to reference this cross-node resource
		for (auto NextBuffer = ++FLinkedObjectIterator(this); NextBuffer; ++NextBuffer)
		{
			FD3D12ResourceLocation::ReferenceNode(NextBuffer->GetParentDevice(), NextBuffer->ResourceLocation, ResourceLocation);

			FScopeLock Lock(&NextBuffer->DynamicSRVsCS);
			for (FD3D12BaseShaderResourceView* DynamicSRVBase : NextBuffer->DynamicSRVs)
			{
				FD3D12ShaderResourceView* DynamicSRV = static_cast<FD3D12ShaderResourceView*>(DynamicSRVBase);
				DynamicSRV->Rename(NextBuffer->ResourceLocation);
			}
		}
	}
}

void FD3D12Buffer::ReleaseUnderlyingResource()
{
	check(IsHeadLink());
	for (FLinkedObjectIterator NextBuffer(this); NextBuffer; ++NextBuffer)
	{
		check(!NextBuffer->LockedData.bLocked && NextBuffer->ResourceLocation.IsValid());
		NextBuffer->ResourceLocation.Clear();
		NextBuffer->RemoveAllDynamicSRVs();
	}
}

void* FD3D12DynamicRHI::LockBuffer(FRHICommandListImmediate* RHICmdList, FD3D12Buffer* Buffer, uint32 BufferSize, uint32 BufferUsage, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	SCOPE_CYCLE_COUNTER(STAT_D3D12LockBufferTime);

	check(Size <= BufferSize);

	FD3D12LockedResource& LockedData = Buffer->LockedData;
	check(LockedData.bLocked == false);
	FD3D12Adapter& Adapter = GetAdapter();

	// Determine whether the buffer is dynamic or not.
	const bool bIsDynamic = (BufferUsage & BUF_AnyDynamic) ? true : false;

	void* Data = nullptr;

	if (bIsDynamic)
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
			if (ShouldDeferBufferLockOperation(RHICmdList) && LockMode == RLM_WriteOnly)
			{
				FRHICommandRenameUploadBuffer* Command = ALLOC_COMMAND_CL(*RHICmdList, FRHICommandRenameUploadBuffer)(Buffer, Device);

				Data = Adapter.GetUploadHeapAllocator(Device->GetGPUIndex()).AllocUploadResource(BufferSize, Buffer->BufferAlignment, Command->NewLocation);
				RHICmdList->RHIThreadFence(true);
			}
			else
			{
				FD3D12ResourceLocation Location(Buffer->GetParentDevice());
				Data = Adapter.GetUploadHeapAllocator(Device->GetGPUIndex()).AllocUploadResource(BufferSize, Buffer->BufferAlignment, Location);
				Buffer->RenameLDAChain(Location);
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
					FConditionalScopeResourceBarrier ScopeResourceBarrierSource(hCommandList, pResource, D3D12_RESOURCE_STATE_COPY_SOURCE, 0);
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

				if (ShouldDeferBufferLockOperation(RHICmdList))
				{
					// Sync when in the render thread implementation
					check(IsInRHIThread() == false);

					RHICmdList->ImmediateFlush(EImmediateFlushType::FlushRHIThread);
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

void FD3D12DynamicRHI::UnlockBuffer(FRHICommandListImmediate* RHICmdList, FD3D12Buffer* Buffer, uint32 BufferUsage)
{
	SCOPE_CYCLE_COUNTER(STAT_D3D12UnlockBufferTime);

	FD3D12LockedResource& LockedData = Buffer->LockedData;
	check(LockedData.bLocked == true);

	// Determine whether the buffer is dynamic or not.
	const bool bIsDynamic = (BufferUsage & BUF_AnyDynamic) != 0;

	if (bIsDynamic)
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
			for (FD3D12Buffer::FLinkedObjectIterator CurrentBuffer(Buffer); CurrentBuffer; ++CurrentBuffer)
			{
				// If we are on the render thread, queue up the copy on the RHIThread so it happens at the correct time.
				if (ShouldDeferBufferLockOperation(RHICmdList))
				{
					if (GNumExplicitGPUsForRendering == 1)
					{
						ALLOC_COMMAND_CL(*RHICmdList, FRHICommandUpdateBuffer)(&CurrentBuffer->ResourceLocation, LockedData.ResourceLocation, LockedData.LockedOffset, LockedData.LockedPitch);
					}
					else // The resource location must be copied because the constructor in FRHICommandUpdateBuffer transfers ownership and clears it.
					{
						FD3D12ResourceLocation NodeResourceLocation(LockedData.ResourceLocation.GetParentDevice());
						FD3D12ResourceLocation::ReferenceNode(NodeResourceLocation.GetParentDevice(), NodeResourceLocation, LockedData.ResourceLocation);
						ALLOC_COMMAND_CL(*RHICmdList, FRHICommandUpdateBuffer)(&CurrentBuffer->ResourceLocation, NodeResourceLocation, LockedData.LockedOffset, LockedData.LockedPitch);
					}
				}
				else
				{
					UpdateBuffer(CurrentBuffer->ResourceLocation.GetResource(),
						CurrentBuffer->ResourceLocation.GetOffsetFromBaseOfResource() + LockedData.LockedOffset,
						LockedData.ResourceLocation.GetResource(),
						LockedData.ResourceLocation.GetOffsetFromBaseOfResource(),
						LockedData.LockedPitch);
				}
			}
		}
	}

	LockedData.Reset();
}
