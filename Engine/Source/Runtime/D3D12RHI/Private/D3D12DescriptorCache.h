// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
D3D12DescriptorCache.h: D3D12 State application functionality
=============================================================================*/
#pragma once

class FD3D12DynamicRHI;
struct FD3D12VertexBufferCache;
struct FD3D12IndexBufferCache;
struct FD3D12ConstantBufferCache;
struct FD3D12ShaderResourceViewCache;
struct FD3D12UnorderedAccessViewCache;
struct FD3D12SamplerStateCache;

// Like a TMap<KeyType, ValueType>
// Faster lookup performance, but possibly has false negatives
template<typename KeyType, typename ValueType>
class FD3D12ConservativeMap
{
public:
	FD3D12ConservativeMap(uint32 Size)
	{
		Table.AddUninitialized(Size);

		Reset();
	}

	void Add(const KeyType& Key, const ValueType& Value)
	{
		uint32 Index = GetIndex(Key);

		Entry& Pair = Table[Index];

		Pair.Valid = true;
		Pair.Key = Key;
		Pair.Value = Value;
	}

	ValueType* Find(const KeyType& Key)
	{
		uint32 Index = GetIndex(Key);

		Entry& Pair = Table[Index];

		if (Pair.Valid &&
			(Pair.Key == Key))
		{
			return &Pair.Value;
		}
		else
		{
			return nullptr;
		}
	}

	void Reset()
	{
		for (int32 i = 0; i < Table.Num(); i++)
		{
			Table[i].Valid = false;
		}
	}

private:
	uint32 GetIndex(const KeyType& Key)
	{
		uint32 Hash = GetTypeHash(Key);

		return Hash % static_cast<uint32>(Table.Num());
	}

	struct Entry
	{
		bool Valid;
		KeyType Key;
		ValueType Value;
	};

	TArray<Entry> Table;
};

uint32 GetTypeHash(const D3D12_SAMPLER_DESC& Desc);
struct FD3D12SamplerArrayDesc
{
	uint32 Count;
	uint16 SamplerID[16];
	inline bool operator==(const FD3D12SamplerArrayDesc& rhs) const
	{
		check(Count <= UE_ARRAY_COUNT(SamplerID));
		check(rhs.Count <= UE_ARRAY_COUNT(rhs.SamplerID));

		if (Count != rhs.Count)
		{
			return false;
		}
		else
		{
			// It is safe to compare pointers, because samplers are kept alive for the lifetime of the RHI
			return 0 == FMemory::Memcmp(SamplerID, rhs.SamplerID, sizeof(SamplerID[0]) * Count);
		}
	}
};
uint32 GetTypeHash(const FD3D12SamplerArrayDesc& Key);
typedef FD3D12ConservativeMap<FD3D12SamplerArrayDesc, D3D12_GPU_DESCRIPTOR_HANDLE> FD3D12SamplerMap;


template< uint32 CPUTableSize>
struct FD3D12UniqueDescriptorTable
{
	FD3D12UniqueDescriptorTable() : GPUHandle({}) {};
	FD3D12UniqueDescriptorTable(FD3D12SamplerArrayDesc KeyIn, CD3DX12_CPU_DESCRIPTOR_HANDLE* Table) : GPUHandle({})
	{
		FMemory::Memcpy(&Key, &KeyIn, sizeof(Key));//Memcpy to avoid alignement issues
		FMemory::Memcpy(CPUTable, Table, Key.Count * sizeof(CD3DX12_CPU_DESCRIPTOR_HANDLE));
	}

	FORCEINLINE uint32 GetTypeHash(const FD3D12UniqueDescriptorTable& Table)
	{
		return FD3D12PipelineStateCache::HashData((void*)Table.Key.SamplerID, Table.Key.Count * sizeof(Table.Key.SamplerID[0]));
	}

	FD3D12SamplerArrayDesc Key;
	CD3DX12_CPU_DESCRIPTOR_HANDLE CPUTable[MAX_SAMPLERS];

	// This will point to the table start in the global heap
	D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle;
};

template<typename FD3D12UniqueDescriptorTable, bool bInAllowDuplicateKeys = false>
struct FD3D12UniqueDescriptorTableKeyFuncs : BaseKeyFuncs<FD3D12UniqueDescriptorTable, FD3D12UniqueDescriptorTable, bInAllowDuplicateKeys>
{
	typedef typename TCallTraits<FD3D12UniqueDescriptorTable>::ParamType KeyInitType;
	typedef typename TCallTraits<FD3D12UniqueDescriptorTable>::ParamType ElementInitType;

	/**
	* @return The key used to index the given element.
	*/
	static FORCEINLINE KeyInitType GetSetKey(ElementInitType Element)
	{
		return Element;
	}

	/**
	* @return True if the keys match.
	*/
	static FORCEINLINE bool Matches(KeyInitType A, KeyInitType B)
	{
		return A.Key == B.Key;
	}

	/** Calculates a hash index for a key. */
	static FORCEINLINE uint32 GetKeyHash(KeyInitType Key)
	{
		return GetTypeHash(Key.Key);
	}
};

typedef FD3D12UniqueDescriptorTable<MAX_SAMPLERS> FD3D12UniqueSamplerTable;

typedef TSet<FD3D12UniqueSamplerTable, FD3D12UniqueDescriptorTableKeyFuncs<FD3D12UniqueSamplerTable>> FD3D12SamplerSet;

class FD3D12DescriptorCache;

class FD3D12OfflineDescriptorManager : public FD3D12SingleNodeGPUObject
{
public: // Types
	typedef D3D12_CPU_DESCRIPTOR_HANDLE HeapOffset;
	typedef decltype(HeapOffset::ptr) HeapOffsetRaw;
	typedef uint32 HeapIndex;

private: // Types
	struct SFreeRange { HeapOffsetRaw Start; HeapOffsetRaw End; };
	struct SHeapEntry
	{
		TRefCountPtr<ID3D12DescriptorHeap> m_Heap;
		TDoubleLinkedList<SFreeRange> m_FreeList;

		SHeapEntry() { }
	};
	typedef TArray<SHeapEntry> THeapMap;

	static D3D12_DESCRIPTOR_HEAP_DESC CreateDescriptor(FRHIGPUMask Node, D3D12_DESCRIPTOR_HEAP_TYPE Type, uint32 NumDescriptorsPerHeap)
	{
		D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
		Desc.Type = Type;
		Desc.NumDescriptors = NumDescriptorsPerHeap;
		Desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;// None as this heap is offline
		Desc.NodeMask = Node.GetNative();

		return Desc;
	}

public: // Methods
	FD3D12OfflineDescriptorManager(FRHIGPUMask Node, D3D12_DESCRIPTOR_HEAP_TYPE Type, uint32 NumDescriptorsPerHeap)
		: FD3D12SingleNodeGPUObject(Node)
		, m_Desc(CreateDescriptor(Node, Type, NumDescriptorsPerHeap))
		, m_DescriptorSize(0)
		, m_pDevice(nullptr)
	{}

	void Init(ID3D12Device* pDevice)
	{
		m_pDevice = pDevice;
		m_DescriptorSize = pDevice->GetDescriptorHandleIncrementSize(m_Desc.Type);
	}

	HeapOffset AllocateHeapSlot(HeapIndex &outIndex)
	{
		FScopeLock Lock(&CritSect);
		if (0 == m_FreeHeaps.Num())
		{
			AllocateHeap();
		}
		check(0 != m_FreeHeaps.Num());
		auto Head = m_FreeHeaps.GetHead();
		outIndex = Head->GetValue();
		SHeapEntry &HeapEntry = m_Heaps[outIndex];
		check(0 != HeapEntry.m_FreeList.Num());
		SFreeRange &Range = HeapEntry.m_FreeList.GetHead()->GetValue();
		HeapOffset Ret = { Range.Start };
		Range.Start += m_DescriptorSize;

		if (Range.Start == Range.End)
		{
			HeapEntry.m_FreeList.RemoveNode(HeapEntry.m_FreeList.GetHead());
			if (0 == HeapEntry.m_FreeList.Num())
			{
				m_FreeHeaps.RemoveNode(Head);
			}
		}
		return Ret;
	}

	void FreeHeapSlot(HeapOffset Offset, HeapIndex index)
	{
		FScopeLock Lock(&CritSect);
		SHeapEntry &HeapEntry = m_Heaps[index];

		SFreeRange NewRange =
		{
			Offset.ptr,
			Offset.ptr + m_DescriptorSize
		};

		bool bFound = false;
		for (auto Node = HeapEntry.m_FreeList.GetHead();
		Node != nullptr && !bFound;
			Node = Node->GetNextNode())
		{
			SFreeRange &Range = Node->GetValue();
			check(Range.Start < Range.End);
			if (Range.Start == Offset.ptr + m_DescriptorSize)
			{
				Range.Start = Offset.ptr;
				bFound = true;
			}
			else if (Range.End == Offset.ptr)
			{
				Range.End += m_DescriptorSize;
				bFound = true;
			}
			else
			{
				check(Range.End < Offset.ptr || Range.Start > Offset.ptr);
				if (Range.Start > Offset.ptr)
				{
					HeapEntry.m_FreeList.InsertNode(NewRange, Node);
					bFound = true;
				}
			}
		}

		if (!bFound)
		{
			if (0 == HeapEntry.m_FreeList.Num())
			{
				m_FreeHeaps.AddTail(index);
			}
			HeapEntry.m_FreeList.AddTail(NewRange);
		}
	}

private: // Methods
	void AllocateHeap()
	{
		TRefCountPtr<ID3D12DescriptorHeap> Heap;
		VERIFYD3D12RESULT(m_pDevice->CreateDescriptorHeap(&m_Desc, IID_PPV_ARGS(Heap.GetInitReference())));
		SetName(Heap, L"FD3D12OfflineDescriptorManager Descriptor Heap");

		HeapOffset HeapBase = Heap->GetCPUDescriptorHandleForHeapStart();
		check(HeapBase.ptr != 0);

		// Allocate and initialize a single new entry in the map
		m_Heaps.SetNum(m_Heaps.Num() + 1);
		SHeapEntry& HeapEntry = m_Heaps.Last();
		HeapEntry.m_FreeList.AddTail({ HeapBase.ptr,
			HeapBase.ptr + m_Desc.NumDescriptors * m_DescriptorSize });
		HeapEntry.m_Heap = Heap;
		m_FreeHeaps.AddTail(m_Heaps.Num() - 1);
	}

private: // Members
	const D3D12_DESCRIPTOR_HEAP_DESC m_Desc;
	uint32 m_DescriptorSize;
	ID3D12Device* m_pDevice; // weak-ref

	THeapMap m_Heaps;
	TDoubleLinkedList<HeapIndex> m_FreeHeaps;
	FCriticalSection CritSect;
};


/**
Manages a D3D heap which is GPU visible - base class which can be used by the FD3D12DescriptorCache
**/
class FD3D12OnlineHeap : public FD3D12DeviceChild, public FD3D12SingleNodeGPUObject
{
public:

	FD3D12OnlineHeap(FD3D12Device* Device, FRHIGPUMask Node, bool CanLoopAround);
	virtual ~FD3D12OnlineHeap() { }

	FORCEINLINE D3D12_CPU_DESCRIPTOR_HANDLE GetCPUSlotHandle(uint32 Slot) const { return{ CPUBase.ptr + Slot * DescriptorSize }; }
	FORCEINLINE D3D12_GPU_DESCRIPTOR_HANDLE GetGPUSlotHandle(uint32 Slot) const { return{ GPUBase.ptr + Slot * DescriptorSize }; }

	inline const uint32 GetDescriptorSize() const { return DescriptorSize; }
	ID3D12DescriptorHeap* GetHeap() { return Heap.GetReference(); }
	const D3D12_DESCRIPTOR_HEAP_DESC& GetDesc() const { return Desc; }

	// Call this to reserve descriptor heap slots for use by the command list you are currently recording. This will wait if
	// necessary until slots are free (if they are currently in use by another command list.) If the reservation can be
	// fulfilled, the index of the first reserved slot is returned (all reserved slots are consecutive.) If not, it will 
	// throw an exception.
	bool CanReserveSlots(uint32 NumSlots);
	uint32 ReserveSlots(uint32 NumSlotsRequested);

	void SetNextSlot(uint32 NextSlot);
	uint32 GetNextSlotIndex() const { return NextSlotIndex;  }

	// Function which can/should be implemented by the derived classes
	virtual bool RollOver() = 0;
	virtual void HeapLoopedAround() { }
	virtual void SetCurrentCommandList(const FD3D12CommandListHandle& CommandListHandle) { }
	virtual uint32 GetTotalSize() { return Desc.NumDescriptors; }

	static const uint32 HeapExhaustedValue = uint32(-1);

protected:
		
	// Handles for manipulation of the heap
	uint32 DescriptorSize;
	D3D12_CPU_DESCRIPTOR_HANDLE CPUBase;
	D3D12_GPU_DESCRIPTOR_HANDLE GPUBase;

	// Does the heap support loop around allocations
	const bool bCanLoopAround;

	// This index indicate where the next set of descriptors should be placed *if* there's room
	uint32 NextSlotIndex;

	// Indicates the last free slot marked by the command list being finished
	uint32 FirstUsedSlot;

	// Keeping this ptr around is basically just for lifetime management
	TRefCountPtr<ID3D12DescriptorHeap> Heap;

	// Desc contains the number of slots and allows for easy recreation
	D3D12_DESCRIPTOR_HEAP_DESC Desc;

};


/**
Global sampler heap managed by the device which stored a unique set of sampler sets
**/
class FD3D12GlobalOnlineSamplerHeap : public FD3D12OnlineHeap
{
public:
	FD3D12GlobalOnlineSamplerHeap(FD3D12Device* Device, FRHIGPUMask Node)
		: FD3D12OnlineHeap(Device, Node, false)
		, bUniqueDescriptorTablesAreDirty(false)
	{ }

	void Init(uint32 TotalSize);

	void ToggleDescriptorTablesDirtyFlag(bool Value) { bUniqueDescriptorTablesAreDirty = Value; }
	bool DescriptorTablesDirty() { return bUniqueDescriptorTablesAreDirty; }
	FD3D12SamplerSet& GetUniqueDescriptorTables() { return UniqueDescriptorTables; }
	FCriticalSection& GetCriticalSection() { return CriticalSection; }

	// Override FD3D12OnlineHeap functions
	virtual bool RollOver() final override;

private:

	FD3D12SamplerSet UniqueDescriptorTables;
	bool bUniqueDescriptorTablesAreDirty;

	FCriticalSection CriticalSection;
};


/**
Heap sub block of a global heap
**/
struct FD3D12GlobalHeapBlock
{
public:
	FD3D12GlobalHeapBlock(uint32 InBaseSlot, uint32 InSize) :
		BaseSlot(InBaseSlot), Size(InSize), SizeUsed(0) {};

	uint32 BaseSlot;
	uint32 Size;
	uint32 SizeUsed;
	FD3D12CLSyncPoint SyncPoint;
};


/**
Global per device heap from which sub blocks can be allocated and freed
**/
class FD3D12GlobalHeap : public FD3D12DeviceChild, public FD3D12SingleNodeGPUObject
{
public:
	FD3D12GlobalHeap(FD3D12Device* Device, FRHIGPUMask Node)
		: FD3D12DeviceChild(Device), FD3D12SingleNodeGPUObject(Node)
	{ }

	// Setup the actual heap
	void Init(D3D12_DESCRIPTOR_HEAP_TYPE InType, uint32 InTotalSize);

	// Allocate an available sub heap block from the global heap
	FD3D12GlobalHeapBlock* AllocateHeapBlock();
	void FreeHeapBlock(FD3D12GlobalHeapBlock* InHeapBlock);

	// Get the CPU & GPU descriptor handle for specific slot index
	uint32 GetDescriptorSize() const { return DescriptorSize; }
	ID3D12DescriptorHeap* GetHeap() { return Heap.GetReference(); }
	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUSlotHandle(FD3D12GlobalHeapBlock* InBlock) const { return{ CPUBase.ptr + InBlock->BaseSlot * DescriptorSize }; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUSlotHandle(FD3D12GlobalHeapBlock* InBlock) const { return{ GPUBase.ptr + InBlock->BaseSlot * DescriptorSize }; }

private:

	// Check all released blocks and check which ones are not used by the GPU anymore
	void UpdateFreeBlocks();

	D3D12_DESCRIPTOR_HEAP_TYPE Type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
	uint32 TotalSize = 0;
	TRefCountPtr<ID3D12DescriptorHeap> Heap;

	uint32 DescriptorSize = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE CPUBase;
	D3D12_GPU_DESCRIPTOR_HANDLE GPUBase;

	TQueue<FD3D12GlobalHeapBlock*> FreeBlocks;
	TArray<FD3D12GlobalHeapBlock*> ReleasedBlocks;

	FCriticalSection CriticalSection;
};


/**
Online heap which can be used by a FD3D12DescriptorCache to manage a block allocated from a GLobalHeap
**/
class FD3D12SubAllocatedOnlineHeap : public FD3D12OnlineHeap
{
public:

	FD3D12SubAllocatedOnlineHeap(FRHIGPUMask InNode, FD3D12DescriptorCache* InDescriptorCache) :
		FD3D12OnlineHeap(nullptr, InNode, false), DescriptorCache(InDescriptorCache) {};

	// Setup the online heap data
	void Init(FD3D12Device* InDevice, D3D12_DESCRIPTOR_HEAP_TYPE InHeapType);

	// Override FD3D12OnlineHeap functions
	virtual bool RollOver() final override;
	virtual void SetCurrentCommandList(const FD3D12CommandListHandle& CommandListHandle) final override;
	virtual uint32 GetTotalSize() final override
	{
		return CurrentBlock ? CurrentBlock->Size : 0;
	}

private:

	// Allocate a new block from the global heap - return true if allocation succeeds
	bool AllocateBlock();

	D3D12_DESCRIPTOR_HEAP_TYPE HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
	FD3D12GlobalHeapBlock* CurrentBlock = nullptr;

	FD3D12DescriptorCache* DescriptorCache = nullptr;
	FD3D12CommandListHandle CurrentCommandList;
};


/**
Online heap which is not shared between multiple FD3D12DescriptorCache - used as overflow heap when the global heaps are full or don't contain the required data
**/
class FD3D12LocalOnlineHeap : public FD3D12OnlineHeap
{
public:
	FD3D12LocalOnlineHeap(FD3D12Device* Device, FRHIGPUMask Node, FD3D12DescriptorCache* InDescriptorCache)
		: FD3D12OnlineHeap(Device, Node, true), DescriptorCache(InDescriptorCache)
	{ }

	// Allocate the actual overflow heap
	void Init(uint32 NumDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE Type);

	// Override FD3D12OnlineHeap functions
	virtual bool RollOver() final override;
	virtual void HeapLoopedAround() final override;
	virtual void SetCurrentCommandList(const FD3D12CommandListHandle& CommandListHandle) final override;
	virtual uint32 GetTotalSize() final override { return Desc.NumDescriptors; }	

private:
	struct SyncPointEntry
	{
		FD3D12CLSyncPoint SyncPoint;
		uint32 LastSlotInUse;

		SyncPointEntry() : LastSlotInUse(0)
		{}

		SyncPointEntry(const SyncPointEntry& InSyncPoint) : SyncPoint(InSyncPoint.SyncPoint), LastSlotInUse(InSyncPoint.LastSlotInUse)
		{}

		SyncPointEntry& operator = (const SyncPointEntry& InSyncPoint)
		{
			SyncPoint = InSyncPoint.SyncPoint;
			LastSlotInUse = InSyncPoint.LastSlotInUse;

			return *this;
		}
	};
	TQueue<SyncPointEntry> SyncPoints;

	struct PoolEntry
	{
		TRefCountPtr<ID3D12DescriptorHeap> Heap;
		FD3D12CLSyncPoint SyncPoint;

		PoolEntry() 
		{}

		PoolEntry(const PoolEntry& InPoolEntry) : Heap(InPoolEntry.Heap), SyncPoint(InPoolEntry.SyncPoint)
		{}

		PoolEntry& operator = (const PoolEntry& InPoolEntry)
		{
			Heap = InPoolEntry.Heap;
			SyncPoint = InPoolEntry.SyncPoint;
			return *this;
		}
	};
	PoolEntry Entry;
	TQueue<PoolEntry> ReclaimPool;

	FD3D12DescriptorCache* DescriptorCache;
	FD3D12CommandListHandle CurrentCommandList;
};


//-----------------------------------------------------------------------------
//	FD3D12DescriptorCache Class Definition
//-----------------------------------------------------------------------------

class FD3D12DescriptorCache : public FD3D12DeviceChild, public FD3D12SingleNodeGPUObject
{
protected:
	FD3D12CommandContext* CmdContext;

public:
	FD3D12OnlineHeap* GetCurrentViewHeap() { return CurrentViewHeap; }
	FD3D12OnlineHeap* GetCurrentSamplerHeap() { return CurrentSamplerHeap; }

	FD3D12DescriptorCache(FRHIGPUMask Node);

	~FD3D12DescriptorCache()
	{
		if (LocalViewHeap) { delete(LocalViewHeap); }
	}

	inline ID3D12DescriptorHeap* GetViewDescriptorHeap()
	{
		return CurrentViewHeap->GetHeap();
	}

	inline ID3D12DescriptorHeap* GetSamplerDescriptorHeap()
	{
		return CurrentSamplerHeap->GetHeap();
	}

	// Checks if the specified descriptor heap has been set on the current command list.
	bool IsHeapSet(ID3D12DescriptorHeap* const pHeap) const
	{
		return (pHeap == pPreviousViewHeap) || (pHeap == pPreviousSamplerHeap);
	}

	// Notify the descriptor cache every time you start recording a command list.
	// This sets descriptor heaps on the command list and indicates the current fence value which allows
	// us to avoid querying DX12 for that value thousands of times per frame, which can be costly.
	D3D12RHI_API void SetCurrentCommandList(const FD3D12CommandListHandle& CommandListHandle);

	// ------------------------------------------------------
	// end Descriptor Slot Reservation stuff

	// null views

	FD3D12DescriptorHandleSRV* pNullSRV;
	FD3D12DescriptorHandleRTV* pNullRTV;
	FD3D12DescriptorHandleUAV* pNullUAV;

#if USE_STATIC_ROOT_SIGNATURE
	FD3D12ConstantBufferView* pNullCBV;
#endif
	TRefCountPtr<FD3D12SamplerState> pDefaultSampler;

	void SetVertexBuffers(FD3D12VertexBufferCache& Cache);
	void SetRenderTargets(FD3D12RenderTargetView** RenderTargetViewArray, uint32 Count, FD3D12DepthStencilView* DepthStencilTarget);

	template <EShaderFrequency ShaderStage>
	void SetUAVs(const FD3D12RootSignature* RootSignature, FD3D12UnorderedAccessViewCache& Cache, const UAVSlotMask& SlotsNeededMask, uint32 Count, uint32 &HeapSlot);

	template <EShaderFrequency ShaderStage>
	void SetSamplers(const FD3D12RootSignature* RootSignature, FD3D12SamplerStateCache& Cache, const SamplerSlotMask& SlotsNeededMask, uint32 Count, uint32& HeapSlot);

	template <EShaderFrequency ShaderStage>
	void SetSRVs(const FD3D12RootSignature* RootSignature, FD3D12ShaderResourceViewCache& Cache, const SRVSlotMask& SlotsNeededMask, uint32 Count, uint32& HeapSlot);

	template <EShaderFrequency ShaderStage> 
#if USE_STATIC_ROOT_SIGNATURE
	void SetConstantBuffers(const FD3D12RootSignature* RootSignature, FD3D12ConstantBufferCache& Cache, const CBVSlotMask& SlotsNeededMask, uint32 Count, uint32& HeapSlot);
#else
	void SetConstantBuffers(const FD3D12RootSignature* RootSignature, FD3D12ConstantBufferCache& Cache, const CBVSlotMask& SlotsNeededMask);
#endif

	void SetStreamOutTargets(FD3D12Resource **Buffers, uint32 Count, const uint32* Offsets);

	bool HeapRolledOver(D3D12_DESCRIPTOR_HEAP_TYPE Type);
	void HeapLoopedAround(D3D12_DESCRIPTOR_HEAP_TYPE Type);
	void Init(FD3D12Device* InParent, FD3D12CommandContext* InCmdContext, uint32 InNumLocalViewDescriptors, uint32 InNumSamplerDescriptors);
	void Clear();
	void BeginFrame();
	void EndFrame();
	void GatherUniqueSamplerTables();

	bool SwitchToContextLocalViewHeap(const FD3D12CommandListHandle& CommandListHandle);
	bool SwitchToContextLocalSamplerHeap();
	bool SwitchToGlobalSamplerHeap();

	TArray<FD3D12UniqueSamplerTable>& GetUniqueTables() { return UniqueTables; }

	inline bool UsingGlobalSamplerHeap() const { return bUsingGlobalSamplerHeap; }
	FD3D12SamplerSet& GetLocalSamplerSet() { return LocalSamplerSet; }

private:
	// Sets the current descriptor tables on the command list and marks any descriptor tables as dirty if necessary.
	// Returns true if one of the heaps actually changed, false otherwise.
	bool SetDescriptorHeaps();

	// The previous view and sampler heaps set on the current command list.
	ID3D12DescriptorHeap* pPreviousViewHeap;
	ID3D12DescriptorHeap* pPreviousSamplerHeap;

	FD3D12OnlineHeap* CurrentViewHeap;
	FD3D12OnlineHeap* CurrentSamplerHeap;

	FD3D12LocalOnlineHeap* LocalViewHeap;
	FD3D12LocalOnlineHeap LocalSamplerHeap;
	FD3D12SubAllocatedOnlineHeap SubAllocatedViewHeap;

	FD3D12SamplerMap SamplerMap;

	TArray<FD3D12UniqueSamplerTable> UniqueTables;

	FD3D12SamplerSet LocalSamplerSet;
	bool bUsingGlobalSamplerHeap;

	uint32 NumLocalViewDescriptors;
};