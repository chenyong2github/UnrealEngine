// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D12Resources.h: D3D resource RHI definitions.
	=============================================================================*/

#pragma once

#include "BoundShaderStateCache.h"
#include "D3D12ShaderResources.h"

constexpr D3D12_RESOURCE_STATES BackBufferBarrierWriteTransitionTargets = D3D12_RESOURCE_STATES(
	uint32(D3D12_RESOURCE_STATE_RENDER_TARGET) |
	uint32(D3D12_RESOURCE_STATE_UNORDERED_ACCESS) |
	uint32(D3D12_RESOURCE_STATE_STREAM_OUT) |
	uint32(D3D12_RESOURCE_STATE_COPY_DEST) |
	uint32(D3D12_RESOURCE_STATE_RESOLVE_DEST));

// Forward Decls
class FD3D12Resource;
class FD3D12StateCacheBase;
class FD3D12CommandListManager;
class FD3D12CommandContext;
class FD3D12CommandListHandle;
class FD3D12SegListAllocator;
struct FD3D12GraphicsPipelineState;
typedef FD3D12StateCacheBase FD3D12StateCache;

#if D3D12_RHI_RAYTRACING
class FD3D12RayTracingGeometry;
class FD3D12RayTracingScene;
class FD3D12RayTracingPipelineState;
class FD3D12RayTracingShader;
#endif // D3D12_RHI_RAYTRACING

enum class ED3D12ResourceStateMode
{
	Default,					//< Decide if tracking is required based on flags
	SingleState,				//< Force disable state tracking of resource - resource will always be in the initial resource state
	MultiState,					//< Force enable state tracking of resource
};

class FD3D12PendingResourceBarrier
{
public:

	FD3D12Resource*              Resource;
	D3D12_RESOURCE_STATES        State;
	uint32                       SubResource;
};

class FD3D12RefCount
{
public:
	FD3D12RefCount()
	{
	}
	virtual ~FD3D12RefCount()
	{
		check(NumRefs.GetValue() == 0);
	}
	uint32 AddRef() const
	{
		int32 NewValue = NumRefs.Increment();
		check(NewValue > 0);
		return uint32(NewValue);
	}
	uint32 Release() const
	{
		int32 NewValue = NumRefs.Decrement();
		if (NewValue == 0)
		{
			delete this;
		}
		check(NewValue >= 0);
		return uint32(NewValue);
	}
	uint32 GetRefCount() const
	{
		int32 CurrentValue = NumRefs.GetValue();
		check(CurrentValue >= 0);
		return uint32(CurrentValue);
	}
private:
	mutable FThreadSafeCounter NumRefs;
};

class FD3D12Heap : public FD3D12RefCount, public FD3D12DeviceChild, public FD3D12MultiNodeGPUObject
{
public:
	FD3D12Heap(FD3D12Device* Parent, FRHIGPUMask VisibleNodes);
	~FD3D12Heap();

	inline ID3D12Heap* GetHeap() { return Heap.GetReference(); }
	inline void SetHeap(ID3D12Heap* HeapIn) { *Heap.GetInitReference() = HeapIn; }

	void UpdateResidency(FD3D12CommandListHandle& CommandList);

	void BeginTrackingResidency(uint64 Size);

	void Destroy();

	inline FD3D12ResidencyHandle* GetResidencyHandle() { return &ResidencyHandle; }

private:
	TRefCountPtr<ID3D12Heap> Heap;
	FD3D12ResidencyHandle ResidencyHandle;
};

class FD3D12Resource : public FD3D12RefCount, public FD3D12DeviceChild, public FD3D12MultiNodeGPUObject
{
private:
	TRefCountPtr<ID3D12Resource> Resource;
	TRefCountPtr<FD3D12Heap> Heap;

	FD3D12ResidencyHandle ResidencyHandle;

	D3D12_RESOURCE_DESC Desc;
	uint8 PlaneCount;
	uint16 SubresourceCount;
	CResourceState ResourceState;
	D3D12_RESOURCE_STATES DefaultResourceState;
	D3D12_RESOURCE_STATES ReadableState;
	D3D12_RESOURCE_STATES WritableState;
#ifdef PLATFORM_SUPPORTS_RESOURCE_COMPRESSION
	D3D12_RESOURCE_STATES CompressedState;
#endif

	bool bRequiresResourceStateTracking : 1;
	bool bDepthStencil : 1;
	bool bDeferDelete : 1;
	bool bBackBuffer : 1;

	D3D12_HEAP_TYPE HeapType;
	D3D12_GPU_VIRTUAL_ADDRESS GPUVirtualAddress;
	void* ResourceBaseAddress;
	int32 NumMapCalls = 0;
	FName DebugName;

#if NV_AFTERMATH
	GFSDK_Aftermath_ResourceHandle AftermathHandle;
#endif

#if UE_BUILD_DEBUG
	static int64 TotalResourceCount;
	static int64 NoStateTrackingResourceCount;
#endif

public:
	explicit FD3D12Resource(FD3D12Device* ParentDevice,
		FRHIGPUMask VisibleNodes,
		ID3D12Resource* InResource,
		D3D12_RESOURCE_STATES InInitialResourceState,
		D3D12_RESOURCE_DESC const& InDesc,
		FD3D12Heap* InHeap = nullptr,
		D3D12_HEAP_TYPE InHeapType = D3D12_HEAP_TYPE_DEFAULT);

	explicit FD3D12Resource(FD3D12Device* ParentDevice,
		FRHIGPUMask VisibleNodes,
		ID3D12Resource* InResource,
		D3D12_RESOURCE_STATES InInitialResourceState,
		ED3D12ResourceStateMode InResourceStateMode,
		D3D12_RESOURCE_STATES InDefaultResourceState,
		D3D12_RESOURCE_DESC const& InDesc,
		FD3D12Heap* InHeap,
		D3D12_HEAP_TYPE InHeapType);

	virtual ~FD3D12Resource();

	operator ID3D12Resource&() { return *Resource; }
	ID3D12Resource* GetResource() const { return Resource.GetReference(); }

	inline void* Map(const D3D12_RANGE* ReadRange = nullptr)
	{
		if (NumMapCalls == 0)
		{
			check(Resource);
			VERIFYD3D12RESULT(Resource->Map(0, ReadRange, &ResourceBaseAddress));
		}
		else
		{
			check(ResourceBaseAddress);
		}
		++NumMapCalls;

		return ResourceBaseAddress;
	}

	inline void Unmap()
	{
		check(Resource);
		check(ResourceBaseAddress);
		check(NumMapCalls > 0);

		--NumMapCalls;
		if (NumMapCalls == 0)
		{
			Resource->Unmap(0, nullptr);
			ResourceBaseAddress = nullptr;
		}
	}

	ID3D12Pageable* GetPageable();
	D3D12_RESOURCE_DESC const& GetDesc() const { return Desc; }
	D3D12_HEAP_TYPE GetHeapType() const { return HeapType; }
	D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const { return GPUVirtualAddress; }
	void* GetResourceBaseAddress() const { check(ResourceBaseAddress); return ResourceBaseAddress; }
	uint16 GetMipLevels() const { return Desc.MipLevels; }
	uint16 GetArraySize() const { return (Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) ? 1 : Desc.DepthOrArraySize; }
	uint8 GetPlaneCount() const { return PlaneCount; }
	uint16 GetSubresourceCount() const { return SubresourceCount; }
	CResourceState& GetResourceState()
	{
		check(bRequiresResourceStateTracking);
		// This state is used as the resource's "global" state between command lists. It's only needed for resources that
		// require state tracking.
		return ResourceState;
	}
	D3D12_RESOURCE_STATES GetDefaultResourceState() const { check(!bRequiresResourceStateTracking); return DefaultResourceState; }
	D3D12_RESOURCE_STATES GetWritableState() const { return WritableState; }
	D3D12_RESOURCE_STATES GetReadableState() const { return ReadableState; }
#ifdef PLATFORM_SUPPORTS_RESOURCE_COMPRESSION
	D3D12_RESOURCE_STATES GetCompressedState() const { return CompressedState; }
	void SetCompressedState(D3D12_RESOURCE_STATES State) { CompressedState = State; }
#endif
	bool RequiresResourceStateTracking() const { return bRequiresResourceStateTracking; }

	inline bool IsBackBuffer() const { return bBackBuffer; }
	inline void SetIsBackBuffer(bool bBackBufferIn) { bBackBuffer = bBackBufferIn; }

	void SetName(const TCHAR* Name)
	{
		DebugName = FName(Name);
		::SetName(Resource, Name);
	}

	FName GetName() const
	{
		return DebugName;
	}
	
	void DoNotDeferDelete()
	{
		bDeferDelete = false;
	}

	inline bool ShouldDeferDelete() const { return bDeferDelete; }
	void DeferDelete();

	inline bool IsPlacedResource() const { return Heap.GetReference() != nullptr; }
	inline FD3D12Heap* GetHeap() const { return Heap; };
	inline bool IsDepthStencilResource() const { return bDepthStencil; }

	void StartTrackingForResidency();

	void UpdateResidency(FD3D12CommandListHandle& CommandList);

	inline FD3D12ResidencyHandle* GetResidencyHandle() 
	{
		return (IsPlacedResource()) ? Heap->GetResidencyHandle() : &ResidencyHandle; 
	}

	struct FD3D12ResourceTypeHelper
	{
		FD3D12ResourceTypeHelper(D3D12_RESOURCE_DESC& Desc, D3D12_HEAP_TYPE HeapType) :
			bSRV((Desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) == 0),
			bDSV((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0),
			bRTV((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0),
			bUAV((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0),
			bWritable(bDSV || bRTV || bUAV),
			bSRVOnly(bSRV && !bWritable),
			bBuffer(Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER),
			bReadBackResource(HeapType == D3D12_HEAP_TYPE_READBACK)
		{}

		const D3D12_RESOURCE_STATES GetOptimalInitialState(bool bAccurateWriteableStates) const
		{
			if (bSRVOnly)
			{
				return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			}
			else if (bBuffer && !bUAV)
			{
				return (bReadBackResource) ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ;
			}
			else if (bWritable)
			{
				if (bAccurateWriteableStates)
				{
					if (bDSV)
					{
						return D3D12_RESOURCE_STATE_DEPTH_WRITE;
					}
					else if (bRTV)
					{
						return D3D12_RESOURCE_STATE_RENDER_TARGET;
					}
					else if (bUAV)
					{
						return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
					}
				}
				else
				{
					// This things require tracking anyway
					return D3D12_RESOURCE_STATE_COMMON;
				}
			}

			return D3D12_RESOURCE_STATE_COMMON;
		}

		const uint32 bSRV : 1;
		const uint32 bDSV : 1;
		const uint32 bRTV : 1;
		const uint32 bUAV : 1;
		const uint32 bWritable : 1;
		const uint32 bSRVOnly : 1;
		const uint32 bBuffer : 1;
		const uint32 bReadBackResource : 1;
	};

private:
	void InitalizeResourceState(D3D12_RESOURCE_STATES InInitialState, ED3D12ResourceStateMode InResourceStateMode, D3D12_RESOURCE_STATES InDefaultState)
	{
		SubresourceCount = GetMipLevels() * GetArraySize() * GetPlaneCount();

		if (InResourceStateMode == ED3D12ResourceStateMode::SingleState)
		{
			// make sure a valid default state is set
			check(IsValidD3D12ResourceState(InDefaultState));

#if UE_BUILD_DEBUG
			FPlatformAtomics::InterlockedIncrement(&NoStateTrackingResourceCount);
#endif
			DefaultResourceState = InDefaultState;
			WritableState = D3D12_RESOURCE_STATE_CORRUPT;
			ReadableState = D3D12_RESOURCE_STATE_CORRUPT;
			bRequiresResourceStateTracking = false;
		}
		else
		{
			DetermineResourceStates(InDefaultState);
		}

		if (bRequiresResourceStateTracking)
		{
#if D3D12_RHI_RAYTRACING
			// No state tracking for acceleration structures because they can't have another state
			check(InDefaultState != D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
#endif // D3D12_RHI_RAYTRACING

			// Only a few resources (~1%) actually need resource state tracking
			ResourceState.Initialize(SubresourceCount);
			ResourceState.SetResourceState(InInitialState);
		}
	}

	void DetermineResourceStates(D3D12_RESOURCE_STATES InDefaultState)
	{
		const FD3D12ResourceTypeHelper Type(Desc, HeapType);

		bDepthStencil = Type.bDSV;

#ifdef PLATFORM_SUPPORTS_RESOURCE_COMPRESSION
		SetCompressedState(D3D12_RESOURCE_STATE_COMMON);
#endif

		if (Type.bWritable)
		{
			// Determine the resource's write/read states.
			if (Type.bRTV)
			{
				// Note: The resource could also be used as a UAV however we don't store that writable state. UAV's are handled in a separate RHITransitionResources() specially for UAVs so we know the writeable state in that case should be UAV.
				check(!Type.bDSV && !Type.bBuffer);
				WritableState = D3D12_RESOURCE_STATE_RENDER_TARGET;
				ReadableState = Type.bSRV ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_CORRUPT;
			}
			else if (Type.bDSV)
			{
				check(!Type.bRTV && !Type.bUAV && !Type.bBuffer);
				WritableState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
				ReadableState = Type.bSRV ? D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_DEPTH_READ;
			}
			else
			{
				check(Type.bUAV && !Type.bRTV && !Type.bDSV);
				WritableState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
				ReadableState = Type.bSRV ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_CORRUPT;
			}
		}

		if (Type.bBuffer)
		{
			if (!Type.bWritable)
			{
				// Buffer used for input, like Vertex/Index buffer.
				// Don't bother tracking state for this resource.
#if UE_BUILD_DEBUG
				FPlatformAtomics::InterlockedIncrement(&NoStateTrackingResourceCount);
#endif
				if (InDefaultState != D3D12_RESOURCE_STATE_TBD)
				{
					DefaultResourceState = InDefaultState;
				}
				else
				{
					DefaultResourceState = (HeapType == D3D12_HEAP_TYPE_READBACK) ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_GENERIC_READ;
				}
				bRequiresResourceStateTracking = false;
				return;
			}
		}
		else
		{
			if (Type.bSRVOnly)
			{
				// Texture used only as a SRV.
				// Don't bother tracking state for this resource.
#if UE_BUILD_DEBUG
				FPlatformAtomics::InterlockedIncrement(&NoStateTrackingResourceCount);
#endif
				if (InDefaultState != D3D12_RESOURCE_STATE_TBD)
				{
					DefaultResourceState = InDefaultState;
				}
				else
				{
					DefaultResourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
				}
				bRequiresResourceStateTracking = false;
				return;
			}
		}
	}
};

#ifdef USE_BUCKET_ALLOCATOR
typedef class FD3D12BucketAllocator FD3D12BaseAllocatorType;
#else
typedef class FD3D12BuddyAllocator FD3D12BaseAllocatorType;
#endif

struct FD3D12BuddyAllocatorPrivateData
{
	uint32 Offset;
	uint32 Order;

	void Init()
	{
		Offset = 0;
		Order = 0;
	}
};

struct FD3D12BlockAllocatorPrivateData
{
	uint64 FrameFence;
	uint32 BucketIndex;
	uint32 Offset;
	FD3D12Resource* ResourceHeap;

	void Init()
	{
		FrameFence = 0;
		BucketIndex = 0;
		Offset = 0;
		ResourceHeap = nullptr;
	}
};

struct FD3D12SegListAllocatorPrivateData
{
	uint32 Offset;

	void Init()
	{
		Offset = 0;
	}
};

class FD3D12ResourceAllocator;
// A very light-weight and cache friendly way of accessing a GPU resource
class FD3D12ResourceLocation : public FD3D12DeviceChild, public FNoncopyable
{
public:

	enum class ResourceLocationType
	{
		eUndefined,
		eStandAlone,
		eSubAllocation,
		eFastAllocation,
		eMultiFrameFastAllocation,
		eAliased, // Oculus is the only API that uses this
		eNodeReference,
		eHeapAliased, 
	};

	enum EAllocatorType : uint8
	{
		AT_Default, // FD3D12BaseAllocatorType
		AT_SegList, // FD3D12SegListAllocator
		AT_Unknown = 0xff
	};

	FD3D12ResourceLocation(FD3D12Device* Parent);
	~FD3D12ResourceLocation();

	void Clear();

	// Transfers the contents of 1 resource location to another, destroying the original but preserving the underlying resource 
	static void TransferOwnership(FD3D12ResourceLocation& Destination, FD3D12ResourceLocation& Source);

	// Setters
	void SetResource(FD3D12Resource* Value);
	inline void SetType(ResourceLocationType Value) { Type = Value;}

	inline void SetAllocator(FD3D12BaseAllocatorType* Value) { Allocator = Value; AllocatorType = AT_Default; }
	inline void SetSegListAllocator(FD3D12SegListAllocator* Value) { SegListAllocator = Value; AllocatorType = AT_SegList; }
	inline void SetMappedBaseAddress(void* Value) { MappedBaseAddress = Value; }
	inline void SetGPUVirtualAddress(D3D12_GPU_VIRTUAL_ADDRESS Value) { GPUVirtualAddress = Value; }
	inline void SetOffsetFromBaseOfResource(uint64 Value) { OffsetFromBaseOfResource = Value; }
	inline void SetSize(uint64 Value) { Size = Value; }

	// Getters
	inline ResourceLocationType GetType() const { return Type; }
	inline FD3D12BaseAllocatorType* GetAllocator() { check(AT_Default == AllocatorType); return Allocator; }
	inline FD3D12SegListAllocator* GetSegListAllocator() { check(AT_SegList == AllocatorType); return SegListAllocator; }
	inline FD3D12Resource* GetResource() const { return UnderlyingResource; }
	inline void* GetMappedBaseAddress() const { return MappedBaseAddress; }
	inline D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const { return GPUVirtualAddress; }
	inline uint64 GetOffsetFromBaseOfResource() const { return OffsetFromBaseOfResource; }
	inline uint64 GetSize() const { return Size; }
	inline FD3D12ResidencyHandle* GetResidencyHandle() { return ResidencyHandle; }
	inline FD3D12BuddyAllocatorPrivateData& GetBuddyAllocatorPrivateData() { return AllocatorData.BuddyAllocatorPrivateData; }
	inline FD3D12BlockAllocatorPrivateData& GetBlockAllocatorPrivateData() { return AllocatorData.BlockAllocatorPrivateData; }
	inline FD3D12SegListAllocatorPrivateData& GetSegListAllocatorPrivateData() { return AllocatorData.SegListAllocatorPrivateData; }

	const inline bool IsValid() const { return Type != ResourceLocationType::eUndefined; }

	inline void AsStandAlone(FD3D12Resource* Resource, uint32 BufferSize = 0, bool bInIsTransient = false )
	{
		SetType(FD3D12ResourceLocation::ResourceLocationType::eStandAlone);
		SetResource(Resource);
		SetSize(BufferSize);

		if (!IsCPUInaccessible(Resource->GetHeapType()))
		{
			D3D12_RANGE range = { 0, IsCPUWritable(Resource->GetHeapType())? 0 : BufferSize };
			SetMappedBaseAddress(Resource->Map(&range));
		}
		SetGPUVirtualAddress(Resource->GetGPUVirtualAddress());
		SetTransient(bInIsTransient);
	}

	inline void AsHeapAliased(FD3D12Resource* Resource)
	{
		SetType(FD3D12ResourceLocation::ResourceLocationType::eHeapAliased);
		SetResource(Resource);
		SetSize(0);

		if (IsCPUWritable(Resource->GetHeapType()))
		{
			D3D12_RANGE range = { 0, 0 };
			SetMappedBaseAddress(Resource->Map(&range));
		}
		SetGPUVirtualAddress(Resource->GetGPUVirtualAddress());
	}


	inline void AsFastAllocation(FD3D12Resource* Resource, uint32 BufferSize, D3D12_GPU_VIRTUAL_ADDRESS GPUBase, void* CPUBase, uint64 ResourceOffsetBase, uint64 Offset, bool bMultiFrame = false)
	{
		if (bMultiFrame)
		{
			Resource->AddRef();
			SetType(ResourceLocationType::eMultiFrameFastAllocation);
		}
		else
		{
			SetType(ResourceLocationType::eFastAllocation);
		}
		SetResource(Resource);
		SetSize(BufferSize);
		SetOffsetFromBaseOfResource(ResourceOffsetBase + Offset);

		if (CPUBase != nullptr)
		{
			SetMappedBaseAddress((uint8*)CPUBase + Offset);
		}
		SetGPUVirtualAddress(GPUBase + Offset);
	}

	// Oculus API Aliases textures so this allows 2+ resource locations to reference the same underlying
	// resource. We should avoid this as much as possible as it requires expensive reference counting and
	// it complicates the resource ownership model.
	static void Alias(FD3D12ResourceLocation& Destination, FD3D12ResourceLocation& Source);
	static void ReferenceNode(FD3D12Device* NodeDevice, FD3D12ResourceLocation& Destination, FD3D12ResourceLocation& Source);

	void SetTransient(bool bInTransient)
	{
		bTransient = bInTransient;
	}
	bool IsTransient() const
	{
		return bTransient;
	}

	void Swap(FD3D12ResourceLocation& Other);

	/**  Get an address used by LLM to track the GPU allocation that this location represents. */
	void* GetAddressForLLMTracking() const
	{
		return (uint8*)this + 1;
	}

private:

	template<bool bReleaseResource>
	void InternalClear();

	void ReleaseResource();

	ResourceLocationType Type;

	FD3D12Resource* UnderlyingResource;
	FD3D12ResidencyHandle* ResidencyHandle;

	// Which allocator this belongs to
	union
	{
		FD3D12BaseAllocatorType* Allocator;
		FD3D12SegListAllocator* SegListAllocator;
	};

	// Union to save memory
	union PrivateAllocatorData
	{
		FD3D12BuddyAllocatorPrivateData BuddyAllocatorPrivateData;
		FD3D12BlockAllocatorPrivateData BlockAllocatorPrivateData;
		FD3D12SegListAllocatorPrivateData SegListAllocatorPrivateData;
	} AllocatorData;

	// Note: These values refer to the start of this location including any padding *NOT* the start of the underlying resource
	void* MappedBaseAddress;
	D3D12_GPU_VIRTUAL_ADDRESS GPUVirtualAddress;
	uint64 OffsetFromBaseOfResource;

	// The size the application asked for
	uint64 Size;

	bool bTransient;

	EAllocatorType AllocatorType;
};

class FD3D12DeferredDeletionQueue : public FD3D12AdapterChild
{
public:
	using FFencePair = TPair<FD3D12Fence*, uint64>;
	using FFenceList = TArray<FFencePair, TInlineAllocator<1>>;

private:
	enum class EObjectType
	{
		RHI,
		D3D,
	};

	struct FencedObjectType
	{
		union
		{
			FD3D12Resource* RHIObject;
			ID3D12Object*   D3DObject;
		};
		FFenceList FenceList;
		EObjectType Type;
	};
	FThreadsafeQueue<FencedObjectType> DeferredReleaseQueue;

public:

	inline const uint32 QueueSize() const { return DeferredReleaseQueue.GetSize(); }

	void EnqueueResource(FD3D12Resource* pResource, FFenceList&& FenceList);
	void EnqueueResource(ID3D12Object* pResource, FD3D12Fence* Fence);

	bool ReleaseResources(bool bDeleteImmediately, bool bIsShutDown);

	FD3D12DeferredDeletionQueue(FD3D12Adapter* InParent);
	~FD3D12DeferredDeletionQueue();

	class FD3D12AsyncDeletionWorker : public FD3D12AdapterChild, public FNonAbandonableTask
	{
	public:
		FD3D12AsyncDeletionWorker(FD3D12Adapter* Adapter, FThreadsafeQueue<FencedObjectType>* DeletionQueue);

		void DoWork();

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FD3D12AsyncDeletionWorker, STATGROUP_ThreadPoolAsyncTasks);
		}

	private:
		TQueue<FencedObjectType> Queue;
	};

private:

	FCriticalSection DeleteTaskCS;
	TQueue<FAsyncTask<FD3D12AsyncDeletionWorker>*> DeleteTasks;

};

struct FD3D12LockedResource : public FD3D12DeviceChild
{
	FD3D12LockedResource(FD3D12Device* Device)
		: FD3D12DeviceChild(Device)
		, ResourceLocation(Device)
		, LockedOffset(0)
		, LockedPitch(0)
		, bLocked(false)
		, bLockedForReadOnly(false)
		, bHasNeverBeenLocked(true)
	{}

	inline void Reset()
	{
		ResourceLocation.Clear();
		bLocked = false;
		bLockedForReadOnly = false;
		LockedOffset = 0;
		LockedPitch = 0;
	}

	FD3D12ResourceLocation ResourceLocation;
	uint32 LockedOffset;
	uint32 LockedPitch;
	uint32 bLocked : 1;
	uint32 bLockedForReadOnly : 1;
	uint32 bHasNeverBeenLocked : 1;
};

class FD3D12BaseShaderResourceView
{
protected:
	void Remove();

	friend class FD3D12BaseShaderResource;
	FD3D12BaseShaderResource* DynamicResource = nullptr;
};

/** The base class of resources that may be bound as shader resources. */
class FD3D12BaseShaderResource : public FD3D12DeviceChild, public IRefCountedObject
{
protected:
	FCriticalSection DynamicSRVsCS;
	TArray<class FD3D12BaseShaderResourceView*> DynamicSRVs;

public:
	FD3D12Resource* GetResource() const { return ResourceLocation.GetResource(); }

	void AddDynamicSRV(FD3D12BaseShaderResourceView* InSRV)
	{
		FScopeLock Lock(&DynamicSRVsCS);
		check(InSRV->DynamicResource == nullptr);
		InSRV->DynamicResource = this;
		DynamicSRVs.Add(InSRV);
	}

	void RemoveDynamicSRV(FD3D12BaseShaderResourceView* InSRV)
	{
		FScopeLock Lock(&DynamicSRVsCS);
		check(InSRV->DynamicResource == this);
		InSRV->DynamicResource = nullptr;
		uint32 Removed = DynamicSRVs.Remove(InSRV);
		check(Removed == 1);
	}

	void RemoveAllDynamicSRVs()
	{
		FScopeLock Lock(&DynamicSRVsCS);
		for (FD3D12BaseShaderResourceView* DynamicSRV : DynamicSRVs)
		{
			if (DynamicSRV != nullptr)
			{
				DynamicSRV->DynamicResource = nullptr;
			}
		}
		DynamicSRVs.Reset();
	}

	void Swap(FD3D12BaseShaderResource& Other)
	{
		::Swap(Parent, Other.Parent);
		ResourceLocation.Swap(Other.ResourceLocation);
		::Swap(BufferAlignment, Other.BufferAlignment);
		::Swap(DynamicSRVs, Other.DynamicSRVs);
	}

	FD3D12ResourceLocation ResourceLocation;
	uint32 BufferAlignment;

public:
	FD3D12BaseShaderResource(FD3D12Device* InParent)
		: FD3D12DeviceChild(InParent)
		, ResourceLocation(InParent)
		, BufferAlignment(0)
	{
	}

	~FD3D12BaseShaderResource()
	{
		for (FD3D12BaseShaderResourceView* DynamicSRV : DynamicSRVs)
		{
			check(DynamicSRV->DynamicResource == this);
			DynamicSRV->DynamicResource = nullptr;
		}
	}
};

inline void FD3D12BaseShaderResourceView::Remove()
{
	if (DynamicResource)
	{
		DynamicResource->RemoveDynamicSRV(this);
	}
}

/** Updates tracked stats for a buffer. */
#define D3D12_BUFFER_TYPE_CONSTANT   1
#define D3D12_BUFFER_TYPE_INDEX      2
#define D3D12_BUFFER_TYPE_VERTEX     3
#define D3D12_BUFFER_TYPE_STRUCTURED 4
extern void UpdateBufferStats(FD3D12ResourceLocation* ResourceLocation, bool bAllocating, uint32 BufferType);

/** Uniform buffer resource class. */
class FD3D12UniformBuffer : public FRHIUniformBuffer, public FD3D12DeviceChild, public FD3D12LinkedAdapterObject<FD3D12UniformBuffer>
{
public:
#if USE_STATIC_ROOT_SIGNATURE
	class FD3D12ConstantBufferView* View;
#endif

	/** The D3D12 constant buffer resource */
	FD3D12ResourceLocation ResourceLocation;

	/** Resource table containing RHI references. */
	TArray<TRefCountPtr<FRHIResource> > ResourceTable;

	const EUniformBufferUsage UniformBufferUsage;

	/** Initialization constructor. */
	FD3D12UniformBuffer(class FD3D12Device* InParent, const FRHIUniformBufferLayout& InLayout, EUniformBufferUsage InUniformBufferUsage)
		: FRHIUniformBuffer(InLayout)
		, FD3D12DeviceChild(InParent)
#if USE_STATIC_ROOT_SIGNATURE
		, View(nullptr)
#endif
		, ResourceLocation(InParent)
		, UniformBufferUsage(InUniformBufferUsage)
	{
	}

	virtual ~FD3D12UniformBuffer();
};

#if PLATFORM_WINDOWS || PLATFORM_HOLOLENS
class FD3D12TransientResource
{
	// Nothing special for fast ram
public:
	void Swap(FD3D12TransientResource&) {}
};
class FD3D12FastClearResource
{
public:
	inline void GetWriteMaskProperties(void*& OutData, uint32& OutSize)
	{
		OutData = nullptr;
		OutSize = 0;
	}
};
#endif

class FD3D12Buffer : public FD3D12BaseShaderResource, public FD3D12TransientResource, public FD3D12LinkedAdapterObject<FD3D12Buffer>
{
public:
	FD3D12Buffer(FD3D12Device* InParent)
		: FD3D12BaseShaderResource(InParent)
		, LockedData(InParent)
	{
	}
	virtual ~FD3D12Buffer()
	{
	}

	void Rename(FD3D12ResourceLocation& NewLocation);
	void RenameLDAChain(FD3D12ResourceLocation& NewLocation);

	void ReleaseUnderlyingResource();

	FD3D12LockedResource LockedData;
};

/** Index buffer resource class that stores stride information. */
class FD3D12IndexBuffer : public FRHIIndexBuffer, public FD3D12Buffer
{
public:
	FD3D12IndexBuffer()
		: FD3D12Buffer(nullptr)
	{
	}

	FD3D12IndexBuffer(FD3D12Device* InParent, uint32 InStride, uint32 InSize, uint32 InUsage)
		: FRHIIndexBuffer(InStride, InSize, InUsage)
		, FD3D12Buffer(InParent)
	{
	}

	virtual ~FD3D12IndexBuffer();

	void Swap(FD3D12IndexBuffer& Other);

	void ReleaseUnderlyingResource();

	// IRefCountedObject interface.
	virtual uint32 AddRef() const
	{
		return FRHIResource::AddRef();
	}
	virtual uint32 Release() const
	{
		return FRHIResource::Release();
	}
	virtual uint32 GetRefCount() const
	{
		return FRHIResource::GetRefCount();
	}
};

class FD3D12ShaderResourceView;

/** Structured buffer resource class. */
class FD3D12StructuredBuffer : public FRHIStructuredBuffer, public FD3D12Buffer
{
public:
	FD3D12StructuredBuffer(FD3D12Device* InParent, uint32 InStride, uint32 InSize, uint32 InUsage)
		: FRHIStructuredBuffer(InStride, InSize, InUsage)
		, FD3D12Buffer(InParent)
	{
	}

	virtual ~FD3D12StructuredBuffer();

	// IRefCountedObject interface.
	virtual uint32 AddRef() const
	{
		return FRHIResource::AddRef();
	}
	virtual uint32 Release() const
	{
		return FRHIResource::Release();
	}
	virtual uint32 GetRefCount() const
	{
		return FRHIResource::GetRefCount();
	}
};

/** Vertex buffer resource class. */
class FD3D12VertexBuffer : public FRHIVertexBuffer, public FD3D12Buffer
{
public:
	FD3D12VertexBuffer()
		: FD3D12Buffer(nullptr)
	{}

	FD3D12VertexBuffer(FD3D12Device* InParent, uint32 InStride, uint32 InSize, uint32 InUsage)
		: FRHIVertexBuffer(InSize, InUsage)
		, FD3D12Buffer(InParent)
	{
		UNREFERENCED_PARAMETER(InStride);
	}

	virtual ~FD3D12VertexBuffer();

	void Swap(FD3D12VertexBuffer& Other);

	void ReleaseUnderlyingResource();

	// IRefCountedObject interface.
	virtual uint32 AddRef() const
	{
		return FRHIResource::AddRef();
	}
	virtual uint32 Release() const
	{
		return FRHIResource::Release();
	}
	virtual uint32 GetRefCount() const
	{
		return FRHIResource::GetRefCount();
	}
};

template<class BufferType>
inline void UpdateBufferStats(FD3D12ResourceLocation* ResourceLocation, bool bAllocating);

template<>
inline void UpdateBufferStats<FD3D12UniformBuffer>(FD3D12ResourceLocation* ResourceLocation, bool bAllocating)
{
	UpdateBufferStats(ResourceLocation, bAllocating, D3D12_BUFFER_TYPE_CONSTANT);
}

template<>
inline void UpdateBufferStats<FD3D12VertexBuffer>(FD3D12ResourceLocation* ResourceLocation, bool bAllocating)
{
	UpdateBufferStats(ResourceLocation, bAllocating, D3D12_BUFFER_TYPE_VERTEX);
}

template<>
inline void UpdateBufferStats<FD3D12IndexBuffer>(FD3D12ResourceLocation* ResourceLocation, bool bAllocating)
{
	UpdateBufferStats(ResourceLocation, bAllocating, D3D12_BUFFER_TYPE_INDEX);
}

template<>
inline void UpdateBufferStats<FD3D12StructuredBuffer>(FD3D12ResourceLocation* ResourceLocation, bool bAllocating)
{
	UpdateBufferStats(ResourceLocation, bAllocating, D3D12_BUFFER_TYPE_STRUCTURED);
}

class FD3D12ResourceBarrierBatcher : public FNoncopyable
{
public:
	explicit FD3D12ResourceBarrierBatcher()
	{};

	// Add a UAV barrier to the batch. Ignoring the actual resource for now.
	void AddUAV()
	{
		Barriers.AddUninitialized();
		D3D12_RESOURCE_BARRIER& Barrier = Barriers.Last();
		Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		Barrier.UAV.pResource = nullptr;	// Ignore the resource ptr for now. HW doesn't do anything with it.
	}

	// Add a transition resource barrier to the batch. Returns the number of barriers added, which may be negative if an existing barrier was cancelled.
	int32 AddTransition(FD3D12Resource* pResource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After, uint32 Subresource)
	{
		check(Before != After);

		if (Barriers.Num())
		{
			// Check if we are simply reverting the last transition. In that case, we can just remove both transitions.
			// This happens fairly frequently due to resource pooling since different RHI buffers can point to the same underlying D3D buffer.
			// Instead of ping-ponging that underlying resource between COPY_DEST and GENERIC_READ, several copies can happen without a ResourceBarrier() in between.
			// Doing this check also eliminates a D3D debug layer warning about multiple transitions of the same subresource.
			const D3D12_RESOURCE_BARRIER& Last = Barriers.Last();
			if (pResource->GetResource() == Last.Transition.pResource &&
				Subresource == Last.Transition.Subresource &&
				Before == Last.Transition.StateAfter &&
				After  == Last.Transition.StateBefore &&
				Last.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
			{
				Barriers.RemoveAt(Barriers.Num() - 1);
				return -1;
			}
		}

		check(IsValidD3D12ResourceState(Before) && IsValidD3D12ResourceState(After));

		D3D12_RESOURCE_BARRIER* Barrier = nullptr;
#if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
		if (pResource->IsBackBuffer() && (After & BackBufferBarrierWriteTransitionTargets))
		{
			BackBufferBarriers.AddUninitialized();
			Barrier = &BackBufferBarriers.Last();
		}
		else
#endif // #if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
		{
			Barriers.AddUninitialized();
			Barrier = &Barriers.Last();
		}

		Barrier->Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		Barrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		Barrier->Transition.StateBefore = Before;
		Barrier->Transition.StateAfter = After;
		Barrier->Transition.Subresource = Subresource;
		Barrier->Transition.pResource = pResource->GetResource();
		return 1;
	}

	void AddAliasingBarrier(ID3D12Resource* pResource)
	{
		Barriers.AddUninitialized();
		D3D12_RESOURCE_BARRIER& Barrier = Barriers.Last();
		Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
		Barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		Barrier.Aliasing.pResourceBefore = NULL;
		Barrier.Aliasing.pResourceAfter = pResource;
	}

	// Flush the batch to the specified command list then reset.
	void Flush(FD3D12Device* Device, ID3D12GraphicsCommandList* pCommandList, int32 BarrierBatchMax);

	// Clears the batch.
	void Reset()
	{
		// Reset the arrays without shrinking (Does not destruct items, does not de-allocate memory).
		Barriers.SetNumUnsafeInternal(0);
		check(Barriers.Num() == 0);
#if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
		BackBufferBarriers.SetNumUnsafeInternal(0);
		check(BackBufferBarriers.Num() == 0);
#endif // #if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
	}

	const TArray<D3D12_RESOURCE_BARRIER>& GetBarriers() const
	{
		return Barriers;
	}

#if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
	const TArray<D3D12_RESOURCE_BARRIER>& GetBackBufferBarriers() const
	{
		return BackBufferBarriers;
	}
#endif // #if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING

private:
	TArray<D3D12_RESOURCE_BARRIER> Barriers;
#if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
	TArray<D3D12_RESOURCE_BARRIER> BackBufferBarriers;
#endif // #if PLATFORM_USE_BACKBUFFER_WRITE_TRANSITION_TRACKING
};

class FD3D12StagingBuffer final : public FRHIStagingBuffer
{
	friend class FD3D12CommandContext;
	friend class FD3D12DynamicRHI;

public:
	FD3D12StagingBuffer(FD3D12Device* InDevice)
		: FRHIStagingBuffer()
		, ResourceLocation(InDevice)
		, ShadowBufferSize(0)
	{}
	~FD3D12StagingBuffer() override;

	void SafeRelease()
	{
		ResourceLocation.Clear();
	}

	void* Lock(uint32 Offset, uint32 NumBytes) override;
	void Unlock() override;

private:
	FD3D12ResourceLocation ResourceLocation;
	uint32 ShadowBufferSize;
};

class D3D12RHI_API FD3D12GPUFence : public FRHIGPUFence
{
public:
	FD3D12GPUFence(FName InName, FD3D12Fence* InFence)
		: FRHIGPUFence(InName)
		, Fence(InFence)
		, Value(MAX_uint64)
	{}

	void WriteInternal(ED3D12CommandQueueType QueueType);
	virtual void Clear() final override;
	virtual bool Poll() const final override;
	virtual bool Poll(FRHIGPUMask GPUMask) const final override;

protected:

	TRefCountPtr<FD3D12Fence> Fence;
	uint64 Value;
};

template<class T>
struct TD3D12ResourceTraits
{
};
template<>
struct TD3D12ResourceTraits<FRHIUniformBuffer>
{
	typedef FD3D12UniformBuffer TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHIIndexBuffer>
{
	typedef FD3D12IndexBuffer TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHIStructuredBuffer>
{
	typedef FD3D12StructuredBuffer TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHIVertexBuffer>
{
	typedef FD3D12VertexBuffer TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHISamplerState>
{
	typedef FD3D12SamplerState TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHIRasterizerState>
{
	typedef FD3D12RasterizerState TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHIDepthStencilState>
{
	typedef FD3D12DepthStencilState TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHIBlendState>
{
	typedef FD3D12BlendState TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHIComputeFence>
{
	typedef FD3D12Fence TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHIGraphicsPipelineState>
{
	typedef FD3D12GraphicsPipelineState TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHIComputePipelineState>
{
	typedef FD3D12ComputePipelineState TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHIGPUFence>
{
	typedef FD3D12GPUFence TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHIStagingBuffer>
{
	typedef FD3D12StagingBuffer TConcreteType;
};


#if D3D12_RHI_RAYTRACING
template<>
struct TD3D12ResourceTraits<FRHIRayTracingScene>
{
	typedef FD3D12RayTracingScene TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHIRayTracingGeometry>
{
	typedef FD3D12RayTracingGeometry TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHIRayTracingPipelineState>
{
	typedef FD3D12RayTracingPipelineState TConcreteType;
};
template<>
struct TD3D12ResourceTraits<FRHIRayTracingShader>
{
	typedef FD3D12RayTracingShader TConcreteType;
};
#endif // D3D12_RHI_RAYTRACING
