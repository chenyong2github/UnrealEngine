// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12RHIPrivate.h"
#include "D3D12TransientResourceAllocator.h"

//PRAGMA_DISABLE_OPTIMIZATION

static int32 GD3D12TransientAllocatorPoolSizeInMB = 128;
static FAutoConsoleVariableRef CVarD3D12TransientAllocatorPoolSizeInMB(
	TEXT("d3d12.TransientAllocator.PoolSizeInMB"),
	GD3D12TransientAllocatorPoolSizeInMB,
	TEXT("Size of a D3D12 transient allocator pool in MB (Default 128)"),
	ECVF_ReadOnly);

static int32 GD3D12TransientAllocatorPoolTextures = 1;
static FAutoConsoleVariableRef CVarD3D12TransientAllocatorPoolTextures(
	TEXT("d3d12.TransientAllocator.PoolTextures"),
	GD3D12TransientAllocatorPoolTextures,
	TEXT("Enable pooling of transient allocated RHITextures in the manager (default enabled)"),
	ECVF_ReadOnly);

static int32 GD3D12TransientAllocatorPoolBuffers = 0;
static FAutoConsoleVariableRef CVarD3D12TransientAllocatorPoolBuffers(
	TEXT("d3d12.TransientAllocator.PoolBuffers"),
	GD3D12TransientAllocatorPoolBuffers,
	TEXT("Enable pooling of transient allocated RHIBuffer in the manager (default disabled)"),
	ECVF_ReadOnly);

uint64 ComputeTextureCreateInfoHash(const FRHITextureCreateInfo& InCreateInfo)
{
	// Make sure all padding is removed
	FRHITextureCreateInfo NewInfo;
	FPlatformMemory::Memzero(&NewInfo, sizeof(FRHITextureCreateInfo));
	NewInfo = InCreateInfo;
	return CityHash64((const char*)&NewInfo, sizeof(FRHITextureCreateInfo));
}

uint64 ComputeBufferCreateInfoHash(const FRHIBufferCreateInfo& InCreateInfo)
{
	return CityHash64((const char*)&InCreateInfo, sizeof(FRHIBufferCreateInfo));
}


//-----------------------------------------------------------------------------
//	FD3D12TransientMemoryPool
//-----------------------------------------------------------------------------

uint64 FD3D12TransientMemoryPool::FResourceCreateState::GetHash() const
{
	uint64 Hash = CityHash64((const char*)&AllocationOffset, sizeof(uint64));
	Hash = CityHash64WithSeed((const char*)&ResourceDesc, sizeof(D3D12_RESOURCE_DESC), Hash);
	Hash = CityHash64WithSeed((const char*)&ClearValue, sizeof(D3D12_CLEAR_VALUE), Hash);
	return Hash;
}


FD3D12TransientMemoryPool::~FD3D12TransientMemoryPool()
{
	check(AllocatedBlocks == 0);

	ClearActiveResources();

	TArray<FD3D12Resource*> CachedResources;
	CachedResourceMap.GenerateValueArray(CachedResources);
	for (FD3D12Resource* Resource : CachedResources)
	{
		check(Resource->GetRefCount() == 1);
		Resource->Release();
	}
	CachedResourceMap.Empty();
}


void FD3D12TransientMemoryPool::ResetPool()
{
	check(AllocatedBlocks == 0);
	check(FreeBlocks.Num() == 1);
	check(FreeBlocks[0]->GetOffset() == 0);
	check(FreeBlocks[0]->GetSize() == PoolSize);
	check(FreeBlocks[0]->GetAlignment() == PoolAlignment);

	ClearActiveResources();
}


void FD3D12TransientMemoryPool::SetPoolIndex(int16 InNewPoolIndex)
{
	PoolIndex = InNewPoolIndex;

	// Update pool index on single free block (validated during reset)
	FRHIPoolAllocationData* FreeBlock = FreeBlocks[0];
	FreeBlock->RemoveFromLinkedList();
	FreeBlock->InitAsFree(PoolIndex, PoolSize, PoolAlignment, 0);
	HeadBlock.AddAfter(FreeBlock);
}


void FD3D12TransientMemoryPool::ClearActiveResources()
{
	ActiveResources.Empty(ActiveResources.Num());
}


void FD3D12TransientMemoryPool::CheckActiveResources(const FInt64Range& InAllocationRange, TArray<FD3D12Resource *>& OverlappingResources)
{		
	// Do we already have active resources in the requested range
	for (ActiveResourceData& ActiveResource : ActiveResources)
	{
		if (ActiveResource.ActiveRange.Overlaps(InAllocationRange))
		{
			check(ActiveResource.Resource != nullptr);

			// Add to dependent list of overlapping resources
			OverlappingResources.Add(ActiveResource.Resource);

			// update the active ranges
			if (ActiveResource.ActiveRange.GetLowerBoundValue() >= InAllocationRange.GetLowerBoundValue())
			{
				if (ActiveResource.ActiveRange.GetUpperBoundValue() > InAllocationRange.GetUpperBoundValue())
				{
					ActiveResource.ActiveRange.SetLowerBoundValue(InAllocationRange.GetUpperBoundValue());
				}
				else
				{
					// Full overlap, make empty range
					ActiveResource.ActiveRange = FInt64Range(ActiveResource.ActiveRange.GetLowerBoundValue(), ActiveResource.ActiveRange.GetLowerBoundValue());
				}
			}
			else
			{
				ActiveResource.ActiveRange.SetUpperBoundValue(InAllocationRange.GetLowerBoundValue());
			}

			// Mark as invalid because it has no active ranges anymore
			if (ActiveResource.ActiveRange.IsEmpty())
			{
				ActiveResource.Resource = nullptr;
			}
		}
	}
}


FD3D12Resource* FD3D12TransientMemoryPool::FindResourceInCache(uint64 InAllocationOffset, const D3D12_RESOURCE_DESC& InDesc, const D3D12_CLEAR_VALUE* InClearValue, const TCHAR* InName)
{
	// Try and find cached placed resource at given offset and location
	FResourceCreateState CreateState;
	CreateState.AllocationOffset = InAllocationOffset;
	CreateState.ResourceDesc = InDesc;
	if (InClearValue)
	{
		CreateState.ClearValue = *InClearValue;
	}
	else
	{
		FPlatformMemory::Memzero(&CreateState.ClearValue, sizeof(D3D12_CLEAR_VALUE));
	}

	uint64 CreateStateHash = CreateState.GetHash();
	FD3D12Resource** Resource = CachedResourceMap.Find(CreateStateHash);
	if (Resource)
	{
		check((*Resource)->GetDesc() == InDesc);		
		CachedResourceMap.Remove(CreateStateHash);
		return *Resource;
	}
	else
	{
		return nullptr;
	}
}


void FD3D12TransientMemoryPool::ReleaseResource(FD3D12Resource* InResource, FRHIPoolAllocationData& InReleasedAllocationData, uint64 InFenceValue)
{
	check(InResource->GetHeap() == BackingHeap);

	uint64 AllocationOffset = InReleasedAllocationData.GetOffset();
	uint64 AllocationSize = InReleasedAllocationData.GetSize();

	// Placed resource object is 'never' freed - it can be reused again this or next frame
	// Will only be destroyed when the allocator is destroyed
	FResourceCreateState CreateState;
	CreateState.AllocationOffset = AllocationOffset;
	CreateState.ResourceDesc = InResource->GetDesc();
	CreateState.ClearValue = InResource->GetClearValue();

	uint64 CreateStateHash = CreateState.GetHash();
	check(CachedResourceMap.Find(CreateStateHash) == nullptr);
	CachedResourceMap.Add(CreateStateHash, InResource);
	
	// Track this resource so aliasing barriers can be added if used again
	ActiveResourceData ResourceData;
	ResourceData.AllocationRange = FInt64Range(AllocationOffset, AllocationOffset + AllocationSize);
	ResourceData.ActiveRange = ResourceData.AllocationRange;
	ResourceData.Resource = InResource;
	ActiveResources.Add(ResourceData);

	// Free the pool data so this range can be reallocated again immediately (active range is tracked for aliasing barriers)
	FRHIPoolAllocationData LockedAllocationData;
	bool bLocked = true;
	LockedAllocationData.MoveFrom(InReleasedAllocationData, bLocked);
	Deallocate(LockedAllocationData);

	// Update the last used frame fence (used during garbage collection)
	UpdateLastUsedFrameFence(InFenceValue);
}

//-----------------------------------------------------------------------------
//	FD3D12TransientMemoryPoolManager
//-----------------------------------------------------------------------------


FD3D12TransientMemoryPoolManager::FD3D12TransientMemoryPoolManager(FD3D12Device* InDevice, FRHIGPUMask VisibleNodes)
	: FD3D12DeviceChild(InDevice)
	, FD3D12MultiNodeGPUObject(InDevice->GetGPUMask(), VisibleNodes)
{
	// texture only interesting in VRAM for now
	InitConfig.HeapType = D3D12_HEAP_TYPE_DEFAULT;

	// unused for textures because placed and not suballocated
	InitConfig.ResourceFlags = D3D12_RESOURCE_FLAG_NONE;
	InitConfig.InitialResourceState = D3D12_RESOURCE_STATE_COMMON;

	// Support RT and UAV for now (Tier 2 support) - need to add different pools for Tier 1
	InitConfig.HeapFlags = D3D12_HEAP_FLAGS(0); // 0 means nothing is denied

	PoolSize = GD3D12TransientAllocatorPoolSizeInMB * 1024 * 1024;
	PoolAlignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	MaxAllocationSize = PoolSize;
}


void FD3D12TransientMemoryPoolManager::Destroy()
{
	for (int32 PoolIndex = 0; PoolIndex < Pools.Num(); ++PoolIndex)
	{
		Pools[PoolIndex]->Destroy();
		delete(Pools[PoolIndex]);
	}
	Pools.Empty();
}


void FD3D12TransientMemoryPoolManager::BeginFrame()
{
	TextureMemoryStats.Reset();
	BufferMemoryStats.Reset();
}


void FD3D12TransientMemoryPoolManager::EndFrame()
{
	FScopeLock Lock(&CS);

	static const int32 FrameLag = 20;

	// Trim empty allocators if not used in last n frames
	FD3D12Adapter* Adapter = GetParentDevice()->GetParentAdapter();
	FD3D12Fence& FrameFence = Adapter->GetFrameFence();
	const uint64 CompletedFence = FrameFence.UpdateLastCompletedFence();
	for (int32 PoolIndex = 0; PoolIndex < Pools.Num(); ++PoolIndex)
	{
		FD3D12MemoryPool* MemoryPool = (FD3D12MemoryPool*)Pools[PoolIndex];
		if (MemoryPool != nullptr && MemoryPool->IsEmpty() && (MemoryPool->GetLastUsedFrameFence() + FrameLag <= CompletedFence))
		{
			MemoryPool->Destroy();
			delete(MemoryPool);

			Pools.RemoveAt(PoolIndex);
			PoolIndex--;
		}
	}
}


FD3D12TransientMemoryPool* FD3D12TransientMemoryPoolManager::GetOrCreateMemoryPool(int16 InPoolIndex)
{
	FScopeLock Lock(&CS);

	FD3D12TransientMemoryPool* MemoryPool = nullptr;
	if (Pools.Num() > 0)
	{
		MemoryPool = Pools.Pop(false);
		MemoryPool->SetPoolIndex(InPoolIndex);
	}
	else
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(D3D12RHI::AllocateTransientMemoryPool);

		MemoryPool = new FD3D12TransientMemoryPool(GetParentDevice(), GetVisibilityMask(), InitConfig,
			TEXT("TransientResourceMemoryPool"), EResourceAllocationStrategy::kPlacedResource, InPoolIndex, PoolSize, PoolAlignment);
		MemoryPool->Init();

#if PLATFORM_WINDOWS
		// Boost priority to make sure it's not paged out
		ID3D12Device* D3DDevice = GetParentDevice()->GetDevice();
		TRefCountPtr<ID3D12Device5> D3DDevice5;
		if (SUCCEEDED(D3DDevice->QueryInterface(IID_PPV_ARGS(D3DDevice5.GetInitReference()))))
		{
			ID3D12Pageable* HeapResource = MemoryPool->GetBackingHeap()->GetHeap();
			D3D12_RESIDENCY_PRIORITY HeapPriority = D3D12_RESIDENCY_PRIORITY_HIGH;
			D3DDevice5->SetResidencyPriority(1, &HeapResource, &HeapPriority);
		}
#endif // PLATFORM_WINDOWS
	}

	return MemoryPool;
}


void FD3D12TransientMemoryPoolManager::ReleaseMemoryPool(FD3D12TransientMemoryPool* InMemoryPool)
{
	FScopeLock Lock(&CS);
	InMemoryPool->ResetPool();
	Pools.Add(InMemoryPool);
}


FD3D12PooledTextureData FD3D12TransientMemoryPoolManager::GetPooledTexture(const FRHITextureCreateInfo& InCreateInfo, const TCHAR* InDebugName)
{
	FD3D12PooledTextureData Result;
	if (GD3D12TransientAllocatorPoolTextures)
	{
		uint64 CreateHash = ComputeTextureCreateInfoHash(InCreateInfo);

		// Try and search the cache for resource of same creation info (inside lock)
		FScopeLock Lock(&CS);
		TArray<FD3D12PooledTextureData>* FindResult = FreeTextures.Find(CreateHash);
		if (FindResult && FindResult->Num() > 0)
		{
			// Make sure they are not referenced anymore! (should in theory not happen)
			//check((*FindResult)[0].RHITexture->GetRefCount() == 1);
			for (int32 Index = 0; Index < FindResult->Num(); ++Index)
			{
				FD3D12PooledTextureData& TextureData = (*FindResult)[Index];
				if (TextureData.RHITexture->GetRefCount() == 1)
				{
					Result = TextureData;
					check(Result.CreateInfo == InCreateInfo);
					FindResult->RemoveAt(Index);
					break;
				}
			}
		}
	}

	return Result;
}


FD3D12PooledBufferData FD3D12TransientMemoryPoolManager::GetPooledBuffer(const FRHIBufferCreateInfo& InCreateInfo, const TCHAR* InDebugName)
{
	FD3D12PooledBufferData Result;
	if (GD3D12TransientAllocatorPoolBuffers)
	{
		uint64 CreateHash = ComputeBufferCreateInfoHash(InCreateInfo);

		// Try and search the cache for resource of same creation info (inside lock)
		FScopeLock Lock(&CS);
		TArray<FD3D12PooledBufferData>* FindResult = FreeBuffers.Find(CreateHash);
		if (FindResult && FindResult->Num() > 0)
		{
			Result = FindResult->Pop(false);
			check(Result.RHIBuffer->GetRefCount() == 1);
		}
	}

	return Result;
}


void FD3D12TransientMemoryPoolManager::ReleaseResources(FD3D12TransientResourceAllocator* InAllocator)
{
	FScopeLock Lock(&CS);

	// release the textures
	if (GD3D12TransientAllocatorPoolTextures)
	{
		for (FD3D12PooledTextureData& TextureData : InAllocator->AllocatedTextures)
		{
			uint64 CreateHash = ComputeTextureCreateInfoHash(TextureData.CreateInfo);
			TArray<FD3D12PooledTextureData>* FindResult = FreeTextures.Find(CreateHash);
			if (FindResult)
			{
				FindResult->Add(TextureData);
			}
			else
			{
				TArray<FD3D12PooledTextureData> Textures;
				Textures.Add(TextureData);
				FreeTextures.Add(CreateHash, Textures);
			}
		}
	}
	InAllocator->AllocatedTextures.Empty();

	// release the buffers
	if (GD3D12TransientAllocatorPoolBuffers)
	{
		for (FD3D12PooledBufferData& BufferData : InAllocator->AllocatedBuffers)
		{
			uint64 CreateHash = ComputeBufferCreateInfoHash(BufferData.CreateInfo);
			TArray<FD3D12PooledBufferData>* FindResult = FreeBuffers.Find(CreateHash);
			if (FindResult)
			{
				FindResult->Add(BufferData);
			}
			else
			{
				TArray<FD3D12PooledBufferData> Buffers;
				Buffers.Add(BufferData);
				FreeBuffers.Add(CreateHash, Buffers);
			}
		}
	}
	InAllocator->AllocatedBuffers.Empty();
}


void FD3D12TransientMemoryPoolManager::UpdateMemoryStats()
{
	FScopeLock Lock(&CS);

	uint32 MemoryAllocated = 0;
	for (FD3D12TransientMemoryPool* MemoryPool : Pools)
	{
		MemoryAllocated += MemoryPool->GetPoolSize();
	}

	SET_MEMORY_STAT(STAT_D3D12TransientMemoryPoolAllocated, MemoryAllocated);
	SET_MEMORY_STAT(STAT_D3D12TransientMemoryFramePoolUsed, TextureMemoryStats.MaxFrameAllocated + BufferMemoryStats.MaxFrameAllocated);
	SET_MEMORY_STAT(STAT_D3D12TransientMemoryFrameCommittedUsed, TextureMemoryStats.CommittedAllocated + BufferMemoryStats.CommittedAllocated);
	SET_DWORD_STAT(STAT_D3D12TransientMemoryPoolAllocations, TextureMemoryStats.PoolAllocations + BufferMemoryStats.PoolAllocations);
	SET_DWORD_STAT(STAT_D3D12TransientMemoryCommittedAllocations, TextureMemoryStats.CommittedAllocations + BufferMemoryStats.CommittedAllocations);

	SET_MEMORY_STAT(STAT_D3D12TransientMemoryBufferFrameUsed, BufferMemoryStats.MaxFrameAllocated);
	SET_MEMORY_STAT(STAT_D3D12TransientMemoryBufferFrameTotalRequested, BufferMemoryStats.TotalRequested);
	SET_MEMORY_STAT(STAT_D3D12TransientMemoryBufferFrameCommittedAllocated, BufferMemoryStats.CommittedAllocated);
	SET_DWORD_STAT(STAT_D3D12TransientMemoryBufferPoolAllocations, BufferMemoryStats.PoolAllocations);
	SET_DWORD_STAT(STAT_D3D12TransientMemoryBufferCommittedAllocations, BufferMemoryStats.CommittedAllocations);

	SET_MEMORY_STAT(STAT_D3D12TransientMemoryTextureFrameUsed, TextureMemoryStats.MaxFrameAllocated);
	SET_MEMORY_STAT(STAT_D3D12TransientMemoryTextureFrameTotalRequested, TextureMemoryStats.TotalRequested);
	SET_MEMORY_STAT(STAT_D3D12TransientMemoryTextureFrameCommittedAllocated, TextureMemoryStats.CommittedAllocated);
	SET_DWORD_STAT(STAT_D3D12TransientMemoryTexturePoolAllocations, TextureMemoryStats.PoolAllocations);
	SET_DWORD_STAT(STAT_D3D12TransientMemoryTextureCommittedAllocations, TextureMemoryStats.CommittedAllocations);
}


//-----------------------------------------------------------------------------
//	FD3D12TransientResourceAllocator
//-----------------------------------------------------------------------------


FD3D12TransientResourceAllocator::FD3D12TransientResourceAllocator(FD3D12TransientMemoryPoolManager& InMemoryPoolManager) :
	FD3D12PoolAllocator(
		InMemoryPoolManager.GetParentDevice(),
		InMemoryPoolManager.GetVisibilityMask(),
		InMemoryPoolManager.GetInitConfig(),
		TEXT("FD3D12TransientResourceAllocator"),
		EResourceAllocationStrategy::kPlacedResource,
		InMemoryPoolManager.GetPoolSize(),
		InMemoryPoolManager.GetPoolAlignment(),
		InMemoryPoolManager.GetMaxAllocationSize(),
		FRHIMemoryPool::EFreeListOrder::SortByOffset,
		false /*defrag*/)
{ 
}


FD3D12TransientResourceAllocator::~FD3D12TransientResourceAllocator()
{
	// release all resources back to pool so they can be reused if enabled (pools have already been freed)
	FD3D12TransientMemoryPoolManager& PoolManager = GetParentDevice()->GetTransientMemoryPoolManager();
	PoolManager.ReleaseResources(this);

	PoolManager.UpdateTextureStats(TextureMemoryStats);
	PoolManager.UpdateBufferStats(BufferMemoryStats);
}


FRHITexture* FD3D12TransientResourceAllocator::CreateTexture(const FRHITextureCreateInfo& InCreateInfo, const TCHAR* InDebugName)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(D3D12RHI::CreateTransientTexture);

	// Should be marked transient
	check((InCreateInfo.Flags & TexCreate_Transient) == TexCreate_Transient);

	// Try and get a pooled texture from the manager
	FD3D12TransientMemoryPoolManager& PoolManager = GetParentDevice()->GetTransientMemoryPoolManager();
	FD3D12PooledTextureData TextureData = PoolManager.GetPooledTexture(InCreateInfo, InDebugName);

	FD3D12BaseShaderResource* BaseShaderResource = nullptr;

	// Found something?
	if (TextureData.RHITexture)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SetupPoolTransientResource);

		// Get the base shader resource
		BaseShaderResource = GetBaseShaderResource(TextureData.RHITexture);
		check(BaseShaderResource);
		check(!BaseShaderResource->ResourceLocation.IsValid());

		// Setup the optional clear value
		D3D12_CLEAR_VALUE* ClearValuePtr = nullptr;
		if (TextureData.ClearValue.Format != DXGI_FORMAT_UNKNOWN)
		{
			ClearValuePtr = &TextureData.ClearValue;
		}

		// Allocate a new FD3D12Resource on the resource location
		AllocateTexture(D3D12_HEAP_TYPE_DEFAULT, TextureData.ResourceDesc, InCreateInfo.Format, ED3D12ResourceStateMode::MultiState, D3D12_RESOURCE_STATE_COMMON, ClearValuePtr, InDebugName, BaseShaderResource->ResourceLocation);

		// Inform the listeners about the change - should ideally still get from cache
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(RenameViews);
			BaseShaderResource->ResourceRenamed(&BaseShaderResource->ResourceLocation);
		}
	}
	else
	{
		// Create a new resource
		FRHIResourceCreateInfo CreateInfo(InDebugName);
		CreateInfo.ClearValueBinding = InCreateInfo.ClearValue;

		FRHITexture* RHITexture = nullptr;
		switch (InCreateInfo.Dimension)
		{
		case ETextureDimension::Texture2D:
		{
			const bool bTextureArray = false;
			const bool bCubeTexture = false;
			ID3D12ResourceAllocator* ResourceAllocator = this;
			FD3D12Texture2D* Texture2D = FD3D12DynamicRHI::GetD3DRHI()->CreateD3D12Texture2D<FD3D12BaseTexture2D>(nullptr, InCreateInfo.Extent.X, InCreateInfo.Extent.Y, 1, bTextureArray, bCubeTexture, InCreateInfo.Format, InCreateInfo.NumMips, InCreateInfo.NumSamples, InCreateInfo.Flags, ERHIAccess::Discard, CreateInfo, ResourceAllocator);
			TextureData.RHITexture = Texture2D;
			TextureData.ResourceDesc = Texture2D->GetResource()->GetDesc();
			TextureData.ClearValue = Texture2D->GetResource()->GetClearValue();
			TextureData.CreateInfo = InCreateInfo;

			BaseShaderResource = Texture2D;
			break;
		}
		case ETextureDimension::Texture3D:
		{
			// Only support 2d textures for now
			ID3D12ResourceAllocator* ResourceAllocator = this;
			FD3D12Texture3D* Texture3D = FD3D12DynamicRHI::GetD3DRHI()->CreateD3D12Texture3D(nullptr, InCreateInfo.Extent.X, InCreateInfo.Extent.Y, InCreateInfo.Depth, InCreateInfo.Format, InCreateInfo.NumMips, InCreateInfo.Flags, ERHIAccess::Discard, CreateInfo, ResourceAllocator);
			TextureData.RHITexture = Texture3D;
			TextureData.ResourceDesc = Texture3D->GetResource()->GetDesc();
			TextureData.ClearValue = Texture3D->GetResource()->GetClearValue();
			TextureData.CreateInfo = InCreateInfo;
			
			BaseShaderResource = Texture3D;
			break;
		}
		default:
		{
			// Only support 2d & 3d textures for now
			check(false);
			break;
		}
		}
	}
	
	SetupAllocatedResource(BaseShaderResource, TextureMemoryStats);

	// keep track of all allocated textures
	AllocatedTextures.Add(TextureData);

	return TextureData.RHITexture;
}


FRHIBuffer* FD3D12TransientResourceAllocator::CreateBuffer(const FRHIBufferCreateInfo& InCreateInfo, const TCHAR* InDebugName)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(D3D12RHI::CreateTransientBuffer);

	// Should be marked transient
	check((InCreateInfo.Usage & BUF_Transient) == BUF_Transient);

	// Try and get a pooled texture from the manager
	FD3D12TransientMemoryPoolManager& PoolManager = GetParentDevice()->GetTransientMemoryPoolManager();
	FD3D12PooledBufferData BufferData = PoolManager.GetPooledBuffer(InCreateInfo, InDebugName);

	FD3D12Buffer* Buffer = nullptr;

	// Found something?
	if (BufferData.RHIBuffer)
	{
		// Get the D3D12 Buffer
		Buffer = FD3D12DynamicRHI::ResourceCast((FRHIBuffer*)BufferData.RHIBuffer);
		check(!Buffer->ResourceLocation.IsValid());

		EBufferUsageFlags UsageFlags = InCreateInfo.Usage;
		D3D12_RESOURCE_DESC Desc;
		uint32 Alignment;
		FD3D12Buffer::GetResourceDescAndAlignment(InCreateInfo.Size, InCreateInfo.Stride, UsageFlags, Desc, Alignment);

		// Allocate a new FD3D12Resource on the resource location
		AllocateResource(D3D12_HEAP_TYPE_DEFAULT, Desc, Desc.Width, Alignment, ED3D12ResourceStateMode::MultiState, D3D12_RESOURCE_STATE_COMMON, nullptr, InDebugName, Buffer->ResourceLocation);

		// Inform the listeners about the change (should be empty?)
		Buffer->ResourceRenamed(&Buffer->ResourceLocation);
	}
	else
	{
		FRHIResourceCreateInfo CreateInfo(InDebugName);
		ID3D12ResourceAllocator* ResourceAllocator = this;
		Buffer = FD3D12DynamicRHI::GetD3DRHI()->CreateD3D12Buffer(nullptr, InCreateInfo.Size, InCreateInfo.Usage, InCreateInfo.Stride, ERHIAccess::Discard, CreateInfo, ResourceAllocator);
		BufferData.RHIBuffer = Buffer;
		BufferData.CreateInfo = InCreateInfo;
	}

	SetupAllocatedResource((FD3D12BaseShaderResource*)Buffer, BufferMemoryStats);
	
	// keep track of all allocated buffers
	AllocatedBuffers.Add(BufferData);

	return BufferData.RHIBuffer;
}


void FD3D12TransientResourceAllocator::DeallocateMemory(FRHITexture* InTexture)
{
	// Get the correct base shader resource taken texture type into account
	FD3D12BaseShaderResource* BaseShaderResource = GetBaseShaderResource(InTexture);
	check(BaseShaderResource);
	DeallocateMemory(BaseShaderResource, TextureMemoryStats);
}


void FD3D12TransientResourceAllocator::DeallocateMemory(FRHIBuffer* InBuffer)
{
	// cast to d3d12 object and call shared function
	FD3D12Buffer* Buffer = FD3D12DynamicRHI::ResourceCast(InBuffer);
	DeallocateMemory(Buffer, BufferMemoryStats);
}


FD3D12BaseShaderResource* FD3D12TransientResourceAllocator::GetBaseShaderResource(FRHITexture* InRHITexture)
{
	FD3D12BaseShaderResource* BaseShaderResource = nullptr;
	if (FRHITexture2D* RHITexture2D = InRHITexture->GetTexture2D())
	{
		FD3D12Texture2D* Texture2D = FD3D12DynamicRHI::ResourceCast(RHITexture2D);
		BaseShaderResource = Texture2D->GetBaseShaderResource();
	}
	else if (FRHITexture3D* RHITexture3D = InRHITexture->GetTexture3D())
	{
		FD3D12Texture3D* Texture3D = FD3D12DynamicRHI::ResourceCast(RHITexture3D);
		BaseShaderResource = Texture3D;
	}
	return BaseShaderResource;
}


void FD3D12TransientResourceAllocator::SetupAllocatedResource(FD3D12BaseShaderResource* InBaseShaderResource, FD3D12TransientMemoryStats& Stats)
{
	uint64 AllocationSize = InBaseShaderResource->ResourceLocation.GetSize();

	// If pool allocated then collected the overlapping resources as well
	if (InBaseShaderResource->ResourceLocation.GetAllocatorType() == FD3D12ResourceLocation::AT_Pool)
	{
		FScopeLock Lock(&CS);
				
		FRHIPoolAllocationData& PoolAllocationData = InBaseShaderResource->ResourceLocation.GetPoolAllocatorPrivateData().PoolData;
		FD3D12TransientMemoryPool* MemoryPool = ((FD3D12TransientMemoryPool*)Pools[PoolAllocationData.GetPoolIndex()]);
		FInt64Range AllocationRange(PoolAllocationData.GetOffset(), PoolAllocationData.GetOffset() + PoolAllocationData.GetSize());
		TArray<FD3D12Resource*> OverlappingResources;
		MemoryPool->CheckActiveResources(AllocationRange, OverlappingResources);
		if (OverlappingResources.Num() > 0)
		{
			OverlappingResourceData.Add(InBaseShaderResource, OverlappingResources);
		}
		
		// update stats
		Stats.PoolAllocations++;
		Stats.CurrentPoolAllocated += AllocationSize;
		Stats.TotalRequested += AllocationSize;
		Stats.MaxFrameAllocated = FMath::Max(Stats.MaxFrameAllocated, Stats.CurrentPoolAllocated);
	}
	else
	{
		// Outside of pool size? Expensive operation if this happens every frame - should this not be allowed?		

		// update stats
		Stats.CommittedAllocations++;
		Stats.CommittedAllocated += AllocationSize;
		Stats.TotalRequested += AllocationSize;
	}

}


void FD3D12TransientResourceAllocator::DeallocateMemory(FD3D12BaseShaderResource* InBaseShaderResource, FD3D12TransientMemoryStats& Stats)
{
	// If pool allocated then release the resource back to the pool
	if (InBaseShaderResource->ResourceLocation.GetAllocatorType() == FD3D12ResourceLocation::AT_Pool)
	{
		FScopeLock Lock(&CS);

		// update stats
		uint64 AllocationSize = InBaseShaderResource->ResourceLocation.GetSize();
		Stats.CurrentPoolAllocated -= AllocationSize;

		FD3D12Adapter* Adapter = GetParentDevice()->GetParentAdapter();
		FD3D12Fence& FrameFence = Adapter->GetFrameFence();

		FRHIPoolAllocationData& PoolAllocationData = InBaseShaderResource->ResourceLocation.GetPoolAllocatorPrivateData().PoolData;
		FD3D12TransientMemoryPool* MemoryPool = ((FD3D12TransientMemoryPool*)Pools[PoolAllocationData.GetPoolIndex()]);
		MemoryPool->ReleaseResource(InBaseShaderResource->ResourceLocation.GetResource(), PoolAllocationData, FrameFence.GetCurrentFence());
	}
}


void FD3D12TransientResourceAllocator::Freeze()
{
	// all memory should have been freed again - so release back to the manager
	// can already be reused by the next transient allocator
	// Resources are kept alive until the allocator is destroyed
	FD3D12TransientMemoryPoolManager& PoolManager = GetParentDevice()->GetTransientMemoryPoolManager();
	for (FRHIMemoryPool* RHIPool : Pools)
	{
		FD3D12TransientMemoryPool* MemoryPool = ((FD3D12TransientMemoryPool*)RHIPool);
		check(MemoryPool->GetAllocatedBlocks() == 0);
		PoolManager.ReleaseMemoryPool(MemoryPool);
	}
	Pools.Empty();
}


FRHIMemoryPool* FD3D12TransientResourceAllocator::CreateNewPool(int16 InPoolIndex)
{
	// Get pool from manager - don't reallocate each time
	return GetParentDevice()->GetTransientMemoryPoolManager().GetOrCreateMemoryPool(InPoolIndex);
}


FD3D12Resource* FD3D12TransientResourceAllocator::CreatePlacedResource(const FRHIPoolAllocationData& InAllocationData, const D3D12_RESOURCE_DESC& InDesc, D3D12_RESOURCE_STATES InCreateState, ED3D12ResourceStateMode InResourceStateMode, const D3D12_CLEAR_VALUE* InClearValue, const TCHAR* InName)
{
	FD3D12Resource* Resource = nullptr;

	// Try and find a cached resource at given offset and creation flags
	{
		FD3D12TransientMemoryPool* MemoryPool = ((FD3D12TransientMemoryPool*)Pools[InAllocationData.GetPoolIndex()]);
		Resource = MemoryPool->FindResourceInCache(InAllocationData.GetOffset(), InDesc, InClearValue, InName);
	}

	// No cached resource then use base class
	if (Resource == nullptr)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(D3D12RHI::CreatePlacedResource);
		Resource = FD3D12PoolAllocator::CreatePlacedResource(InAllocationData, InDesc, InCreateState, InResourceStateMode, InClearValue, InName);
	}

	return Resource;
}


void FD3D12TransientResourceAllocator::DeallocateResource(FD3D12ResourceLocation& ResourceLocation)
{
	check(IsOwner(ResourceLocation));

	// Don't touch the allocation data - it's probably already freed via call to DeallocateMemory
	// On clear the data on the resource location itself
	ResourceLocation.ClearAllocator();
}


TArrayView<FD3D12Resource*> FD3D12TransientResourceAllocator::GetOverlappingResources(FD3D12BaseShaderResource* InBaseShaderResource)
{
	check(IsOwner(InBaseShaderResource->ResourceLocation));

	TArray<FD3D12Resource*>* FindResult = OverlappingResourceData.Find(InBaseShaderResource);
	if (FindResult)
	{
		return TArrayView<FD3D12Resource*>(*FindResult);
	}
	else
	{
		return TArrayView<FD3D12Resource*>();
	}
}