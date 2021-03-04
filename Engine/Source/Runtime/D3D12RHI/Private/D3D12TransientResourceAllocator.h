// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "D3D12PoolAllocator.h"

// Forward declare
class FD3D12TransientResourceAllocator;

/**
@brief Tracking stats for transient resource allocations
**/
struct FD3D12TransientMemoryStats
{
	void Reset()
	{
		CurrentPoolAllocated = 0;
		TotalRequested = 0;
		MaxFrameAllocated = 0;
		CommittedAllocated = 0;
		PoolAllocations = 0;
		CommittedAllocations = 0;
	}
	void Add(const FD3D12TransientMemoryStats& InOther)
	{
		CurrentPoolAllocated += InOther.CurrentPoolAllocated;
		TotalRequested += InOther.TotalRequested;
		MaxFrameAllocated += InOther.MaxFrameAllocated;
		CommittedAllocated += InOther.CommittedAllocated;
		PoolAllocations += InOther.PoolAllocations;
		CommittedAllocations += InOther.CommittedAllocations;
	}

	uint64 CurrentPoolAllocated = 0;
	uint64 TotalRequested = 0;
	uint64 MaxFrameAllocated = 0;
	uint64 CommittedAllocated = 0;
	uint32 PoolAllocations = 0;
	uint32 CommittedAllocations = 0;
};


struct FD3D12PooledTextureData
{
	FRHITextureCreateInfo CreateInfo;
	D3D12_RESOURCE_DESC ResourceDesc;
	D3D12_CLEAR_VALUE ClearValue;
	FTextureRHIRef RHITexture;
};

struct FD3D12PooledBufferData
{
	FRHIBufferCreateInfo CreateInfo;
	FBufferRHIRef RHIBuffer;
};


/**
@brief D3D12 transient pool specific implementation of the FD3D12MemoryPool
	   Caches previously created resource to remove placed resource creation overhead and 
	   also keeps track of all open allocation ranges used to create the aliasing barriers
**/
class FD3D12TransientMemoryPool : public FD3D12MemoryPool
{
public:

	FD3D12TransientMemoryPool(FD3D12Device* ParentDevice, FRHIGPUMask VisibleNodes, const FD3D12ResourceInitConfig& InInitConfig, const FString& Name,
		EResourceAllocationStrategy InAllocationStrategy, int16 InPoolIndex, uint64 InPoolSize, uint32 InPoolAlignment) :
		FD3D12MemoryPool(ParentDevice, VisibleNodes, InInitConfig, Name, InAllocationStrategy, InPoolIndex, InPoolSize, InPoolAlignment, FRHIMemoryPool::EFreeListOrder::SortByOffset) { }
	virtual ~FD3D12TransientMemoryPool();

	void ResetPool();
	void SetPoolIndex(int16 InNewPoolIndex);

	void ClearActiveResources();
	void CheckActiveResources(const FInt64Range& InAllocationRange, TArray<FD3D12Resource*>& OverlappingResources);

	FD3D12Resource* FindResourceInCache(uint64 InAllocationOffset, const D3D12_RESOURCE_DESC& InDesc, const D3D12_CLEAR_VALUE* InClearValue, const TCHAR* InName);
	void ReleaseResource(FD3D12Resource* InResource, FRHIPoolAllocationData& InReleasedAllocationData, uint64 InFenceValue);
		
private:

	// Track all active ranges on an allocation which require possible aliasing barriers
	struct ActiveResourceData
	{
		FInt64Range AllocationRange;
		FInt64Range ActiveRange;
		FD3D12Resource* Resource;
	};
	TArray<ActiveResourceData> ActiveResources;

	// Cache all previously created placed resources at given offset and creation params to reduce
	// placed resource creation overhead
	struct FResourceCreateState
	{
		// Key values used to create the resource
		uint64 AllocationOffset;
		D3D12_RESOURCE_DESC ResourceDesc;
		D3D12_CLEAR_VALUE ClearValue;

		uint64 GetHash() const;
	};
	TMap<uint64, FD3D12Resource*> CachedResourceMap;
};


//-----------------------------------------------------------------------------
//	Transient Memory Pool Manager
//-----------------------------------------------------------------------------

/**
@brief Manages an array of pools per device which can be reused by the temporary
	   transient memory allocator - the pools are returned to the manager when the 
	   allocator is destroyed, so the heap creation and placed resource caches are kept
	   alive in between frames. Pools are deleted when not used for n amount of frames.
**/
class FD3D12TransientMemoryPoolManager : public FD3D12DeviceChild, public FD3D12MultiNodeGPUObject
{
public:
	FD3D12TransientMemoryPoolManager(FD3D12Device* InParent, FRHIGPUMask VisibleNodes);
	~FD3D12TransientMemoryPoolManager() { }

	void Destroy();

	void BeginFrame();
	void EndFrame();

	// Get or creation functions from cached resources
	FD3D12TransientMemoryPool* GetOrCreateMemoryPool(int16 InPoolIndex);
	void ReleaseMemoryPool(FD3D12TransientMemoryPool* InMemoryPool);
	FD3D12PooledTextureData GetPooledTexture(const FRHITextureCreateInfo& InCreateInfo, const TCHAR* InDebugName);
	FD3D12PooledBufferData GetPooledBuffer(const FRHIBufferCreateInfo& InCreateInfo, const TCHAR* InDebugName);
	void ReleaseResources(FD3D12TransientResourceAllocator* InAllocator);

	// Get the pool creation members
	const FD3D12ResourceInitConfig& GetInitConfig() const { return InitConfig; }
	uint64 GetPoolSize() const { return PoolSize; }
	uint32 GetPoolAlignment() const { return PoolAlignment; }
	uint64 GetMaxAllocationSize() const { return MaxAllocationSize; }

	// Stat management
	void UpdateTextureStats(const FD3D12TransientMemoryStats& InStats)
	{
		TextureMemoryStats.Add(InStats);
	}
	void UpdateBufferStats(const FD3D12TransientMemoryStats& InStats)
	{
		BufferMemoryStats.Add(InStats);
	}
	void UpdateMemoryStats();
	
private:

	// Pool creation members
	FD3D12ResourceInitConfig InitConfig;
	uint64 PoolSize;
	uint32 PoolAlignment;
	uint64 MaxAllocationSize;

	// Critical section to lock access to the pools & resource caches
	FCriticalSection CS;
	TArray<FD3D12TransientMemoryPool*> Pools;
	TMap<uint64, TArray<FD3D12PooledTextureData>> FreeTextures;
	TMap<uint64, TArray<FD3D12PooledBufferData>> FreeBuffers;

	// Stats of last frame
	FD3D12TransientMemoryStats TextureMemoryStats;
	FD3D12TransientMemoryStats BufferMemoryStats;
};


//-----------------------------------------------------------------------------
//	Transient Resource Allocator
//-----------------------------------------------------------------------------

/**
@brief D3D12 Specific implementation of the IRHITransientResourceAllocator
	   Uses a pool allocator as base to manage the internal D3D12 memory pools and also takes care of the actual
	   D3D12 placed resource creation.
	   Pools are retrieved from and released to the FD3D12TransientMemoryPoolManager
**/
class FD3D12TransientResourceAllocator : public IRHITransientResourceAllocator, public FD3D12PoolAllocator
{
public:

	FD3D12TransientResourceAllocator(FD3D12TransientMemoryPoolManager& InMemoryPoolManager);
	virtual ~FD3D12TransientResourceAllocator();

	// Implementation of FRHITransientResourceAllocator interface
	virtual FRHITexture* CreateTexture(const FRHITextureCreateInfo& InCreateInfo, const TCHAR* InDebugName) override;
	virtual FRHIBuffer* CreateBuffer(const FRHIBufferCreateInfo& InCreateInfo, const TCHAR* InDebugName) override;
	virtual void DeallocateMemory(FRHITexture* InTexture) override;
	virtual void DeallocateMemory(FRHIBuffer* InBuffer) override;
	virtual void Freeze() override;

	// Override deallocation function - memory possibly already freed during DeallocateMemory
	virtual void DeallocateResource(FD3D12ResourceLocation& ResourceLocation) override;

	// Try and find overlapping resource data for given base resource
	TArrayView<FD3D12Resource*> GetOverlappingResources(FD3D12BaseShaderResource* InBaseShaderResource);

private:

	// Override placed resource allocation helper function to get from cache if available
	virtual FD3D12Resource* CreatePlacedResource(const FRHIPoolAllocationData& InAllocationData, const D3D12_RESOURCE_DESC& InDesc, D3D12_RESOURCE_STATES InCreateState, ED3D12ResourceStateMode InResourceStateMode, const D3D12_CLEAR_VALUE* InClearValue, const TCHAR* InName) override;
	virtual FRHIMemoryPool* CreateNewPool(int16 InPoolIndex) override;

	// Shared allocation/deallocation helper
	FD3D12BaseShaderResource* GetBaseShaderResource(FRHITexture* InRHITexture);
	void SetupAllocatedResource(FD3D12BaseShaderResource* InBaseShaderResource, FD3D12TransientMemoryStats& Stats);
	void DeallocateMemory(FD3D12BaseShaderResource* InBaseShaderResource, FD3D12TransientMemoryStats& Stats);

	// Reference all the allocated resources - owner of all the resources and will be returned back to the manager when the allocator is destroyed
	TArray<FD3D12PooledTextureData> AllocatedTextures;
	TArray<FD3D12PooledBufferData> AllocatedBuffers;

	// All overlapping resource allocation information from all allocated resources on this allocator
	// Not stored inside each resource to not increase the base object size for each resource
	// (can still be moved if it the lookup becomes a perf bottleneck at some point)
	TMap<FD3D12BaseShaderResource*, TArray<FD3D12Resource*>> OverlappingResourceData;

	// Stat tracking
	FD3D12TransientMemoryStats TextureMemoryStats;
	FD3D12TransientMemoryStats BufferMemoryStats;

	friend class FD3D12TransientMemoryPoolManager;
};