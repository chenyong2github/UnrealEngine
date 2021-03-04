// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12RHIPrivate.h"
#include "D3D12PoolAllocator.h"

#ifndef NEEDS_D3D12_INDIRECT_ARGUMENT_HEAP_WORKAROUND
#define NEEDS_D3D12_INDIRECT_ARGUMENT_HEAP_WORKAROUND 0
#endif

//-----------------------------------------------------------------------------
//	FD3D12MemoryPool
//-----------------------------------------------------------------------------

FD3D12MemoryPool::FD3D12MemoryPool(FD3D12Device* ParentDevice, FRHIGPUMask VisibleNodes, const FD3D12ResourceInitConfig& InInitConfig, const FString& InName,
	EResourceAllocationStrategy InAllocationStrategy, int16 InPoolIndex, uint64 InPoolSize, uint32 InPoolAlignment, EFreeListOrder InFreeListOrder)
	: FRHIMemoryPool(InPoolIndex, InPoolSize, InPoolAlignment, InFreeListOrder), FD3D12DeviceChild(ParentDevice), FD3D12MultiNodeGPUObject(ParentDevice->GetGPUMask(), VisibleNodes)
	, InitConfig(InInitConfig), Name(InName), AllocationStrategy(InAllocationStrategy), LastUsedFrameFence(0)
{
}


FD3D12MemoryPool::~FD3D12MemoryPool()
{
	Destroy();
}


void FD3D12MemoryPool::Init()
{
	if (PoolSize == 0)
	{
		return;
	}

	FD3D12Device* Device = GetParentDevice();
	FD3D12Adapter* Adapter = Device->GetParentAdapter();

	if (AllocationStrategy == EResourceAllocationStrategy::kPlacedResource)
	{
		// Alignment should be either 4K or 64K for places resources
		check(PoolAlignment == D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT || PoolAlignment == D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

		D3D12_HEAP_PROPERTIES HeapProps = CD3DX12_HEAP_PROPERTIES(InitConfig.HeapType);
		HeapProps.CreationNodeMask = GetGPUMask().GetNative();
		HeapProps.VisibleNodeMask = GetVisibilityMask().GetNative();

		D3D12_HEAP_DESC Desc = {};
		Desc.SizeInBytes = PoolSize;
		Desc.Properties = HeapProps;
		Desc.Alignment = 0;
		Desc.Flags = InitConfig.HeapFlags;
		if (Adapter->IsHeapNotZeroedSupported())
		{
			Desc.Flags |= FD3D12_HEAP_FLAG_CREATE_NOT_ZEROED;
		}

		ID3D12Heap* Heap = nullptr;
		{
			LLM_PLATFORM_SCOPE(ELLMTag::GraphicsPlatform);

			// we are tracking allocations ourselves, so don't let XMemAlloc track these as well
			LLM_SCOPED_PAUSE_TRACKING_FOR_TRACKER(ELLMTracker::Default, ELLMAllocType::System);
			VERIFYD3D12RESULT(Adapter->GetD3DDevice()->CreateHeap(&Desc, IID_PPV_ARGS(&Heap)));
		}
		SetName(Heap, L"LinkListAllocator Backing Heap");

		BackingHeap = new FD3D12Heap(GetParentDevice(), GetVisibilityMask());
		BackingHeap->SetHeap(Heap);

		// Only track resources that cannot be accessed on the CPU.
		if (IsGPUOnly(InitConfig.HeapType))
		{
			BackingHeap->BeginTrackingResidency(Desc.SizeInBytes);
		}
	}
	else
	{
		{
			LLM_SCOPED_PAUSE_TRACKING_FOR_TRACKER(ELLMTracker::Default, ELLMAllocType::System);
			const D3D12_HEAP_PROPERTIES HeapProps = CD3DX12_HEAP_PROPERTIES(InitConfig.HeapType, GetGPUMask().GetNative(), GetVisibilityMask().GetNative());
			VERIFYD3D12RESULT(Adapter->CreateBuffer(HeapProps, GetGPUMask(), InitConfig.InitialResourceState, ED3D12ResourceStateMode::SingleState, InitConfig.InitialResourceState, PoolSize, BackingResource.GetInitReference(), TEXT("Resource Allocator Underlying Buffer"), InitConfig.ResourceFlags));
		}

		if (IsCPUAccessible(InitConfig.HeapType))
		{
			BackingResource->Map();
		}
	}	

	FRHIMemoryPool::Init();
}


void FD3D12MemoryPool::Destroy()
{
	LLM_SCOPED_PAUSE_TRACKING_FOR_TRACKER(ELLMTracker::Default, ELLMAllocType::System);

	FRHIMemoryPool::Destroy();

	if (BackingResource)
	{
		ensure(BackingResource->GetRefCount() == 1 || GNumExplicitGPUsForRendering > 1);
		BackingResource = nullptr;
	}

	if (BackingHeap)
	{
		BackingHeap->Destroy();
	}
}


//-----------------------------------------------------------------------------
//	FD3D12PoolAllocator
//-----------------------------------------------------------------------------


FD3D12ResourceInitConfig FD3D12PoolAllocator::GetResourceAllocatorInitConfig(D3D12_HEAP_TYPE InHeapType, D3D12_RESOURCE_FLAGS InResourceFlags, EBufferUsageFlags InBufferUsage)
{
	FD3D12ResourceInitConfig InitConfig;
	InitConfig.HeapType = InHeapType;
	InitConfig.ResourceFlags = InResourceFlags;

#if D3D12_RHI_RAYTRACING
	// Setup initial resource state depending on the requested buffer flags
	if (EnumHasAnyFlags(InBufferUsage, BUF_AccelerationStructure))
	{
		// should only have this flag and no other flags
		check(InBufferUsage == BUF_AccelerationStructure);
		InitConfig.InitialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	}
	else
#endif // D3D12_RHI_RAYTRACING
		if (InitConfig.HeapType == D3D12_HEAP_TYPE_READBACK)
		{
			InitConfig.InitialResourceState = D3D12_RESOURCE_STATE_COPY_DEST;
		}
		else if (EnumHasAnyFlags(InBufferUsage, BUF_UnorderedAccess))
		{
			check(InResourceFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			InitConfig.InitialResourceState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		}
		else
		{
			InitConfig.InitialResourceState = D3D12_RESOURCE_STATE_GENERIC_READ;
		}

	InitConfig.HeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
	if (EnumHasAnyFlags(InBufferUsage, BUF_DrawIndirect))
	{
		check(InResourceFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
#if !NEEDS_D3D12_INDIRECT_ARGUMENT_HEAP_WORKAROUND
		InitConfig.HeapFlags |= D3D12RHI_HEAP_FLAG_ALLOW_INDIRECT_BUFFERS;
#endif
	}

	return InitConfig;
}


EResourceAllocationStrategy FD3D12PoolAllocator::GetResourceAllocationStrategy(D3D12_RESOURCE_FLAGS InResourceFlags, ED3D12ResourceStateMode InResourceStateMode)
{
	// Does the resource need state tracking and transitions
	ED3D12ResourceStateMode ResourceStateMode = InResourceStateMode;
	if (ResourceStateMode == ED3D12ResourceStateMode::Default)
	{
		ResourceStateMode = (InResourceFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) ? ED3D12ResourceStateMode::MultiState : ED3D12ResourceStateMode::SingleState;
	}

	// multi state resource need to placed because each allocation can be in a different state
	return (ResourceStateMode == ED3D12ResourceStateMode::MultiState) ? EResourceAllocationStrategy::kPlacedResource : EResourceAllocationStrategy::kManualSubAllocation;
}


FD3D12PoolAllocator::FD3D12PoolAllocator(FD3D12Device* ParentDevice, FRHIGPUMask VisibleNodes, const FD3D12ResourceInitConfig& InInitConfig, const FString& InName,
	EResourceAllocationStrategy InAllocationStrategy, uint64 InPoolSize, uint32 InPoolAlignment, uint32 InMaxAllocationSize, FRHIMemoryPool::EFreeListOrder InFreeListOrder, bool bInDefragEnabled) :
	FRHIPoolAllocator(InPoolSize, InPoolAlignment, InMaxAllocationSize, InFreeListOrder, bInDefragEnabled), 
	FD3D12DeviceChild(ParentDevice), 
	FD3D12MultiNodeGPUObject(ParentDevice->GetGPUMask(), VisibleNodes), 
	InitConfig(InInitConfig), 
	Name(InName), 
	AllocationStrategy(InAllocationStrategy)
{}


FD3D12PoolAllocator::~FD3D12PoolAllocator()
{
	Destroy();
}


bool FD3D12PoolAllocator::SupportsAllocation(D3D12_HEAP_TYPE InHeapType, D3D12_RESOURCE_FLAGS InResourceFlags, EBufferUsageFlags InBufferUsage, ED3D12ResourceStateMode InResourceStateMode) const
{
	FD3D12ResourceInitConfig InInitConfig = GetResourceAllocatorInitConfig(InHeapType, InResourceFlags, InBufferUsage);
	EResourceAllocationStrategy InAllocationStrategy = GetResourceAllocationStrategy(InResourceFlags, InResourceStateMode);
	return (InitConfig == InInitConfig && AllocationStrategy == InAllocationStrategy);
}


void FD3D12PoolAllocator::AllocDefaultResource(D3D12_HEAP_TYPE InHeapType, const D3D12_RESOURCE_DESC& InDesc, EBufferUsageFlags InBufferUsage, ED3D12ResourceStateMode InResourceStateMode,
	D3D12_RESOURCE_STATES InCreateState, uint32 InAllocationAlignment, const TCHAR* InName, FD3D12ResourceLocation& ResourceLocation)
{
#if DO_CHECK
	// Validate the create state
	if (InHeapType == D3D12_HEAP_TYPE_READBACK)
	{
		check(InCreateState == D3D12_RESOURCE_STATE_COPY_DEST);
	}
	else if (InHeapType == D3D12_HEAP_TYPE_UPLOAD)
	{
		check(InCreateState == D3D12_RESOURCE_STATE_GENERIC_READ);
	}
	else if (InBufferUsage == BUF_UnorderedAccess && InResourceStateMode == ED3D12ResourceStateMode::SingleState)
	{
		check(InCreateState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}
#if D3D12_RHI_RAYTRACING
	else if (InBufferUsage & BUF_AccelerationStructure)
	{
		// RayTracing acceleration structures must be created in a particular state and may never transition out of it.
		check(InResourceStateMode == ED3D12ResourceStateMode::SingleState);
		check(InCreateState == D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
	}
#endif // D3D12_RHI_RAYTRACING
#endif  // DO_CHECK

	AllocateResource(InHeapType, InDesc, InDesc.Width, InAllocationAlignment, InResourceStateMode, InCreateState, nullptr, InName, ResourceLocation);
}


void FD3D12PoolAllocator::AllocateResource(D3D12_HEAP_TYPE InHeapType, const D3D12_RESOURCE_DESC& InDesc, uint64 InSize, uint32 InAllocationAlignment, ED3D12ResourceStateMode InResourceStateMode,
	D3D12_RESOURCE_STATES InCreateState, const D3D12_CLEAR_VALUE* InClearValue, const TCHAR* InName, FD3D12ResourceLocation& ResourceLocation)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(D3D12RHI::AllocatePoolResource);

	// If the resource location owns a block, this will deallocate it.
	ResourceLocation.Clear();
	if (InSize == 0)
	{
		return;
	}

	FD3D12Device* Device = GetParentDevice();
	FD3D12Adapter* Adapter = Device->GetParentAdapter();

	const bool PoolResource = InSize <= MaxAllocationSize;
	if (PoolResource)
	{
		const bool bPlacedResource = (AllocationStrategy == EResourceAllocationStrategy::kPlacedResource);

		uint32 AllocationAlignment = InAllocationAlignment;

		// Ensure we're allocating from the correct pool
		if (bPlacedResource)
		{
			// Writeable resources get separate ID3D12Resource* with their own resource state by using placed resources. Just make sure it's UAV, other flags are free to differ.
			check(InDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || (InDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0 || InHeapType == D3D12_HEAP_TYPE_READBACK);

			// If it's a placed resource then base offset will always be 0 from the actual d3d resource so ignore the allocation alignment - no extra offset required
			// for creating the views!
			check(InAllocationAlignment <= PoolAlignment);
			AllocationAlignment = PoolAlignment;
		}
		else
		{
			// Read-only resources get suballocated from big resources, thus share ID3D12Resource* and resource state with other resources. Ensure it's suballocated from a resource with identical flags.
			check(InDesc.Flags == InitConfig.ResourceFlags);
		}

		// Try to allocate in one of the pools
		FRHIPoolAllocationData& AllocationData = ResourceLocation.GetPoolAllocatorPrivateData().PoolData;
		if (TryAllocateInternal(InSize, AllocationAlignment, AllocationData))
		{
			// Setup the resource location
			ResourceLocation.SetType(FD3D12ResourceLocation::ResourceLocationType::eSubAllocation);
			ResourceLocation.SetPoolAllocator(this);
			ResourceLocation.SetSize(InSize);

			AllocationData.SetOwner(&ResourceLocation);

			if (AllocationStrategy == EResourceAllocationStrategy::kManualSubAllocation)
			{
				FD3D12Resource* BackingResource = GetBackingResource(ResourceLocation);

				ResourceLocation.SetOffsetFromBaseOfResource(AllocationData.GetOffset());
				ResourceLocation.SetResource(BackingResource);
				ResourceLocation.SetGPUVirtualAddress(BackingResource->GetGPUVirtualAddress() + AllocationData.GetOffset());

				if (IsCPUAccessible(InitConfig.HeapType))
				{
					ResourceLocation.SetMappedBaseAddress((uint8*)BackingResource->GetResourceBaseAddress() + AllocationData.GetOffset());
				}
			}
			else
			{
				check(ResourceLocation.GetResource() == nullptr);

				FD3D12Resource* NewResource = CreatePlacedResource(AllocationData, InDesc, InCreateState, InResourceStateMode, InClearValue, InName);
				ResourceLocation.SetResource(NewResource);
			}

			// Successfully sub-allocated
			return;
		}

		// shouldn't fail
		check(false);
	}

	// Allocate Standalone - move to owner of resource because this allocator should only manage pooled allocations (needed for now to do the same as FD3D12DefaultBufferPool)
	FD3D12Resource* NewResource = nullptr;
	const D3D12_HEAP_PROPERTIES HeapProps = CD3DX12_HEAP_PROPERTIES(InHeapType, GetGPUMask().GetNative(), GetVisibilityMask().GetNative());
	D3D12_RESOURCE_DESC Desc = InDesc;
	Desc.Alignment = 0;
	VERIFYD3D12RESULT(Adapter->CreateCommittedResource(Desc, GetGPUMask(), HeapProps, InCreateState, InResourceStateMode, InCreateState, InClearValue, &NewResource, InName, false));
	
	ResourceLocation.AsStandAlone(NewResource, InSize);
}


FD3D12Resource* FD3D12PoolAllocator::CreatePlacedResource(
	const FRHIPoolAllocationData& InAllocationData,
	const D3D12_RESOURCE_DESC& InDesc, 
	D3D12_RESOURCE_STATES InCreateState, 
	ED3D12ResourceStateMode InResourceStateMode, 
	const D3D12_CLEAR_VALUE* InClearValue, 
	const TCHAR* InName)
{
	FD3D12Adapter* Adapter = GetParentDevice()->GetParentAdapter();
	FD3D12HeapAndOffset HeapAndOffset = GetBackingHeapAndAllocationOffsetInBytes(InAllocationData);

	FD3D12Resource* NewResource = nullptr;
	VERIFYD3D12RESULT(Adapter->CreatePlacedResource(InDesc, HeapAndOffset.Heap, HeapAndOffset.Offset, InCreateState, InResourceStateMode, D3D12_RESOURCE_STATE_TBD, InClearValue, &NewResource, InName));
	return NewResource;
}


void FD3D12PoolAllocator::DeallocateResource(FD3D12ResourceLocation& ResourceLocation)
{
	check(IsOwner(ResourceLocation));

	FScopeLock Lock(&CS);

	// Mark allocation data as free
	FRHIPoolAllocationData& AllocationData = ResourceLocation.GetPoolAllocatorPrivateData().PoolData;	
	check(AllocationData.IsAllocated());

	// If locked then assume still initial setup or in defragmentation unlock request
	// Mark as nop because block will be deleted anyway
	// TODO: optimize to not perform the full iteration here!
	if (AllocationData.IsLocked())
	{
		for (FrameFencedAllocationData& Operation : FrameFencedOperations)
		{
			if (Operation.AllocationData == &AllocationData)
			{
				check(Operation.Operation == FrameFencedAllocationData::EOperation::Unlock);
				Operation.Operation = FrameFencedAllocationData::EOperation::Nop;
				Operation.AllocationData = nullptr;
				break;
			}
		}

		// If still pending copy then clear the copy operation data
		for (FD3D12VRAMCopyOperation& CopyOperation : PendingCopyOps)
		{
			if (CopyOperation.SourceResource == ResourceLocation.GetResource() ||
				CopyOperation.DestResource == ResourceLocation.GetResource())
			{
				CopyOperation.SourceResource = nullptr;
				CopyOperation.DestResource = nullptr;
				break;
			}
		}
	}

	int16 PoolIndex = AllocationData.GetPoolIndex();
	FRHIPoolAllocationData* ReleasedAllocationData = (AllocationDataPool.Num() > 0) ? AllocationDataPool.Pop(false) : new FRHIPoolAllocationData();;
	bool bLocked = true;
	ReleasedAllocationData->MoveFrom(AllocationData, bLocked);

	// Clear the allocator data
	ResourceLocation.ClearAllocator();

	// Store fence when last used so we know when to unlock the free data
	FD3D12Adapter* Adapter = GetParentDevice()->GetParentAdapter();
	FD3D12Fence& FrameFence = Adapter->GetFrameFence();

	FrameFencedOperations.AddUninitialized();
	FrameFencedAllocationData& DeleteRequest = FrameFencedOperations.Last();
	DeleteRequest.Operation = FrameFencedAllocationData::EOperation::Deallocate;
	DeleteRequest.FrameFence = FrameFence.GetCurrentFence();
	DeleteRequest.AllocationData = ReleasedAllocationData;

	// Update the last used frame fence (used during garbage collection)
	((FD3D12MemoryPool*)Pools[PoolIndex])->UpdateLastUsedFrameFence(DeleteRequest.FrameFence);

	// Also store the placed resource so it can be correctly freed when fence is done
	if (ResourceLocation.GetResource()->IsPlacedResource())
	{
		DeleteRequest.PlacedResource = ResourceLocation.GetResource();
	}
	else
	{
		DeleteRequest.PlacedResource = nullptr;
	}
}


FRHIMemoryPool* FD3D12PoolAllocator::CreateNewPool(int16 InPoolIndex)
{
	FD3D12MemoryPool* NewPool = new FD3D12MemoryPool(GetParentDevice(),	GetVisibilityMask(), InitConfig,
		Name, AllocationStrategy, InPoolIndex, PoolSize, PoolAlignment, FreeListOrder);
	NewPool->Init();
	return NewPool;
}


bool FD3D12PoolAllocator::HandleDefragRequest(FRHIPoolAllocationData* InSourceBlock, FRHIPoolAllocationData& InTmpTargetBlock)
{
	// Cache source copy data
	FD3D12ResourceLocation* Owner = (FD3D12ResourceLocation*)InSourceBlock->GetOwner();
	uint64 CurrentOffset = Owner->GetOffsetFromBaseOfResource();
	FD3D12Resource* CurrentResource = Owner->GetResource();

	// Release the current allocation (will only be freed on the next frame fence)
	DeallocateResource(*Owner);

	// Move temp allocation data to allocation data of the owner (part of different allocator now)		
	bool bLocked = true;
	InSourceBlock->MoveFrom(InTmpTargetBlock, bLocked);
	InSourceBlock->SetOwner(Owner);
	Owner->SetPoolAllocator(this);

	// Notify owner of moved allocation data (recreated resources and SRVs if needed)
	Owner->OnAllocationMoved(InSourceBlock);

	// Add request to unlock the source block on the next fence value (copy operation should have been done by then)
	FD3D12Adapter* Adapter = GetParentDevice()->GetParentAdapter();
	FD3D12Fence& FrameFence = Adapter->GetFrameFence();
	FrameFencedOperations.AddUninitialized();
	FrameFencedAllocationData& UnlockRequest = FrameFencedOperations.Last();
	UnlockRequest.Operation = FrameFencedAllocationData::EOperation::Unlock;
	UnlockRequest.FrameFence = FrameFence.GetCurrentFence();
	UnlockRequest.AllocationData = InSourceBlock;

	// Schedule a copy operation of the actual data
	FD3D12VRAMCopyOperation CopyOp;
	CopyOp.SourceResource	= CurrentResource;
	CopyOp.SourceOffset		= CurrentOffset;
	CopyOp.DestResource		= Owner->GetResource();
	CopyOp.DestOffset		= Owner->GetOffsetFromBaseOfResource();
	CopyOp.Size				= InSourceBlock->GetSize();
	CopyOp.CopyType			= AllocationStrategy == EResourceAllocationStrategy::kManualSubAllocation ? FD3D12VRAMCopyOperation::ECopyType::BufferRegion : FD3D12VRAMCopyOperation::ECopyType::Resource;
	check(CopyOp.SourceResource != nullptr);
	check(CopyOp.DestResource != nullptr);
	PendingCopyOps.Add(CopyOp);

	// TODO: Using aliasing buffer on whole heap for copies to reduce flushes and resource transitions

	return true;
}


void FD3D12PoolAllocator::CleanUpAllocations(uint64 InFrameLag)
{
	FScopeLock Lock(&CS);

	FD3D12Adapter* Adapter = GetParentDevice()->GetParentAdapter();
	FD3D12Fence& FrameFence = Adapter->GetFrameFence();

	uint32 PopCount = 0;
	for (int32 i = 0; i < FrameFencedOperations.Num(); i++)
	{
		FrameFencedAllocationData& Operation = FrameFencedOperations[i];
		if (FrameFence.IsFenceComplete(Operation.FrameFence))
		{
			switch (Operation.Operation)
			{
			case FrameFencedAllocationData::EOperation::Deallocate:
			{
				// Deallocate the locked block (actually free now)
				DeallocateInternal(*Operation.AllocationData);
				Operation.AllocationData->Reset();
				AllocationDataPool.Add(Operation.AllocationData);

				// Free placed resource if created
				if (AllocationStrategy == EResourceAllocationStrategy::kPlacedResource)
				{
					// Release the resource
					check(Operation.PlacedResource != nullptr);
					Operation.PlacedResource->Release();
					Operation.PlacedResource = nullptr;
				}
				else
				{
					check(Operation.PlacedResource == nullptr);
				}
				break;
			}
			case FrameFencedAllocationData::EOperation::Unlock:
			{
				Operation.AllocationData->Unlock();
				break;
			}
			case FrameFencedAllocationData::EOperation::Nop:
			{
				break;
			}
			default: check(false);
			}

			PopCount = i + 1;
		}
		else
		{
			break;
		}
	}

	if (PopCount)
	{
		// clear out all of the released blocks, don't allow the array to shrink
		FrameFencedOperations.RemoveAt(0, PopCount, false);
	}

	// Trim empty allocators if not used in last n frames
	const uint64 CompletedFence = FrameFence.UpdateLastCompletedFence();
	for (int32 PoolIndex = 0; PoolIndex < Pools.Num(); ++PoolIndex)
	{
		FD3D12MemoryPool* MemoryPool = (FD3D12MemoryPool*) Pools[PoolIndex];
		if (MemoryPool != nullptr && MemoryPool->IsEmpty() && (MemoryPool->GetLastUsedFrameFence() + InFrameLag <= CompletedFence))
		{
			MemoryPool->Destroy();
			delete(MemoryPool);
			Pools[PoolIndex] = nullptr;
		}
	}
}


void FD3D12PoolAllocator::TransferOwnership(FD3D12ResourceLocation& InSource, FD3D12ResourceLocation& InDest)
{
	FScopeLock Lock(&CS);

	check(IsOwner(InSource));

	// Don't need to lock - ownership simply changed
	bool bLocked = false;
	FRHIPoolAllocationData& DestinationPoolData = InDest.GetPoolAllocatorPrivateData().PoolData;
	DestinationPoolData.MoveFrom(InSource.GetPoolAllocatorPrivateData().PoolData, bLocked);
	DestinationPoolData.SetOwner(&InDest);
}


FD3D12Resource* FD3D12PoolAllocator::GetBackingResource(FD3D12ResourceLocation& InResourceLocation) const
{
	check(IsOwner(InResourceLocation));
	FRHIPoolAllocationData& AllocationData = InResourceLocation.GetPoolAllocatorPrivateData().PoolData;
	return ((FD3D12MemoryPool*)Pools[AllocationData.GetPoolIndex()])->GetBackingResource();
}


FD3D12HeapAndOffset FD3D12PoolAllocator::GetBackingHeapAndAllocationOffsetInBytes(FD3D12ResourceLocation& InResourceLocation) const
{
	check(IsOwner(InResourceLocation));

	return GetBackingHeapAndAllocationOffsetInBytes(InResourceLocation.GetPoolAllocatorPrivateData().PoolData);
}


FD3D12HeapAndOffset FD3D12PoolAllocator::GetBackingHeapAndAllocationOffsetInBytes(const FRHIPoolAllocationData& InAllocationData) const
{
	FD3D12HeapAndOffset HeapAndOffset;
	HeapAndOffset.Heap = ((FD3D12MemoryPool*)Pools[InAllocationData.GetPoolIndex()])->GetBackingHeap();
	HeapAndOffset.Offset = uint64(AlignDown(InAllocationData.GetOffset(), PoolAlignment));
	return HeapAndOffset;
}


void FD3D12PoolAllocator::FlushPendingCopyOps(FD3D12CommandContext& InCommandContext)
{
	FScopeLock Lock(&CS);

	// TODO: sort the copy ops to reduce amount of transitions!!

	FD3D12CommandListHandle& CommandListHandle = InCommandContext.CommandListHandle;

	FD3D12Adapter* Adapter = GetParentDevice()->GetParentAdapter();
	FD3D12Fence& FrameFence = Adapter->GetFrameFence();

	for (FD3D12VRAMCopyOperation& CopyOperation : PendingCopyOps)
	{		
		// Cleared copy op?
		if (CopyOperation.SourceResource == nullptr || CopyOperation.DestResource == nullptr)
		{
			continue;
		}

		bool bRTAccelerationStructure = false;
		if (CopyOperation.SourceResource->RequiresResourceStateTracking())
		{
			check(CopyOperation.DestResource->RequiresResourceStateTracking());
			FD3D12DynamicRHI::TransitionResource(CommandListHandle, CopyOperation.SourceResource, D3D12_RESOURCE_STATE_TBD, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, FD3D12DynamicRHI::ETransitionMode::Apply);
			FD3D12DynamicRHI::TransitionResource(CommandListHandle, CopyOperation.DestResource, D3D12_RESOURCE_STATE_TBD, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, FD3D12DynamicRHI::ETransitionMode::Apply);
		}
#if D3D12_RHI_RAYTRACING
		else if (CopyOperation.SourceResource->GetDefaultResourceState() == D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE)
		{
			// can't make state changes to RT resources
			bRTAccelerationStructure = true;
			check(CopyOperation.DestResource->GetDefaultResourceState() == D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
		}
#endif // D3D12_RHI_RAYTRACING
		else
		{
			check(!CopyOperation.DestResource->RequiresResourceStateTracking());
			CommandListHandle.AddTransitionBarrier(CopyOperation.SourceResource, CopyOperation.SourceResource->GetDefaultResourceState(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
			CommandListHandle.AddTransitionBarrier(CopyOperation.DestResource, CopyOperation.DestResource->GetDefaultResourceState(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
		}

		// Enable to log all defrag copy ops
		//UE_LOG(LogD3D12RHI, Log, TEXT("Running defrag copy op for: %s at Frame: %d (Old: %#016llx New: %#016llx)"), *CopyOperation.SourceResource->GetName().ToString(), FrameFence.GetCurrentFence(), CopyOperation.SourceResource, CopyOperation.DestResource);

		InCommandContext.numCopies++;
		CommandListHandle.FlushResourceBarriers();

#if D3D12_RHI_RAYTRACING
		if (bRTAccelerationStructure)
		{
			D3D12_GPU_VIRTUAL_ADDRESS SrcAddress = CopyOperation.SourceResource->GetResource()->GetGPUVirtualAddress() + CopyOperation.SourceOffset;
			D3D12_GPU_VIRTUAL_ADDRESS DestAddress = CopyOperation.DestResource->GetResource()->GetGPUVirtualAddress() + CopyOperation.DestOffset;
			CommandListHandle.RayTracingCommandList()->CopyRaytracingAccelerationStructure(DestAddress, SrcAddress, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_CLONE);
		}
		else
#endif // D3D12_RHI_RAYTRACING
		{
			switch (CopyOperation.CopyType)
			{
			case FD3D12VRAMCopyOperation::BufferRegion:
			{
				CommandListHandle->CopyBufferRegion(CopyOperation.DestResource->GetResource(), CopyOperation.DestOffset,
					CopyOperation.SourceResource->GetResource(), CopyOperation.SourceOffset, CopyOperation.Size);
				break;
			}
			case FD3D12VRAMCopyOperation::Resource:
			{
				CommandListHandle->CopyResource(CopyOperation.DestResource->GetResource(), CopyOperation.SourceResource->GetResource());
				break;
			}
			}
		}

		CommandListHandle.UpdateResidency(CopyOperation.SourceResource);
		CommandListHandle.UpdateResidency(CopyOperation.DestResource);

		if (!bRTAccelerationStructure && !CopyOperation.SourceResource->RequiresResourceStateTracking())
		{
			CommandListHandle.AddTransitionBarrier(CopyOperation.SourceResource, D3D12_RESOURCE_STATE_COPY_SOURCE, CopyOperation.SourceResource->GetDefaultResourceState(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
			CommandListHandle.AddTransitionBarrier(CopyOperation.DestResource, D3D12_RESOURCE_STATE_COPY_DEST, CopyOperation.DestResource->GetDefaultResourceState(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
		}
	}

	PendingCopyOps.Empty(PendingCopyOps.Num());
}

