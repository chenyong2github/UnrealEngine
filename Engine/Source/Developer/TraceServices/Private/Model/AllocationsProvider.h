// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Common/PagedArray.h"
#include "Containers/Map.h"
#include "HAL/CriticalSection.h"
#include "ProfilingDebugging/MemoryTrace.h"
#include "TraceServices/Model/Callstack.h"
#include "TraceServices/Model/AllocationsProvider.h"

namespace TraceServices
{

class IAnalysisSession;
class ILinearAllocator;
class FSbTree;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FAllocationsProviderLock
{
public:
	void ReadAccessCheck() const;
	void WriteAccessCheck() const;

	void BeginRead();
	void EndRead();

	void BeginWrite();
	void EndWrite();

private:
	FRWLock RWLock;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTagTracker
{
private:
	static constexpr uint32 TrackerIdShift = 24;
	static constexpr uint32 TrackerIdMask = 0xFF000000;
	static constexpr uint32 PtrTagMask = 0x80000000;
	
	struct ThreadState
	{
		TArray<TagIdType> TagStack;
	};

	struct TagEntry
	{
		const TCHAR* Display;
		TagIdType ParentTag;
	};

public:
	void AddTagSpec(TagIdType Tag, TagIdType ParentTag, const TCHAR* Display);
	void PushTag(uint32 ThreadId, uint8 Tracker, TagIdType Tag);
	void PopTag(uint32 ThreadId, uint8 Tracker);
	TagIdType GetCurrentTag(uint32 ThreadId, uint8 Tracker) const;
	const TCHAR* GetTagString(TagIdType Tag) const;

	void PushTagFromPtr(uint32 ThreadId, uint8 Tracker, TagIdType Tag);
	void PopTagFromPtr(uint32 ThreadId, uint8 Tracker);
	bool HasTagFromPtrScope(uint32 ThreadId, uint8 Tracker) const;

private:
	inline uint32 GetTrackerThreadId(uint32 ThreadId, uint8 Tracker) const
	{
		return (Tracker << TrackerIdShift) | (~TrackerIdMask & ThreadId);
	}

	TMap<uint32, ThreadState> TrackerThreadStates;
	TMap<TagIdType, TagEntry> TagMap;
	uint32 NumErrors = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FAllocationItem
{
	static constexpr uint32 AlignmentBits = 8;
	static constexpr uint32 AlignmentShift = 56;
	static constexpr uint64 SizeMask = (1ULL << AlignmentShift) - 1;

	static uint64 UnpackSize(uint64 SizeAndAlignment) { return (SizeAndAlignment & SizeMask); }
	static uint32 UnpackAlignment(uint64 SizeAndAlignment) { return static_cast<uint32>(SizeAndAlignment >> AlignmentShift); }
	static uint64 PackSizeAndAlignment(uint64 Size, uint8 Alignment) { return (Size | (static_cast<uint64>(Alignment) << AlignmentShift)); }

	FORCEINLINE bool IsContained(uint64 InAddress) const { return Address >= InAddress && Address < (Address + GetSize()); }
	uint64 GetSize() const { return UnpackSize(SizeAndAlignment); }
	uint32 GetAlignment() const { return UnpackAlignment(SizeAndAlignment); }
	bool IsHeap() const { return EnumHasAnyFlags(Flags, EMemoryTraceHeapAllocationFlags::Heap); }

	uint32 StartEventIndex;
	uint32 EndEventIndex;
	double StartTime;
	double EndTime;
	uint64 Owner;
	uint64 Address;
	uint64 SizeAndAlignment; // (Alignment << AlignmentShift) | Size
	mutable const FCallstack* Callstack;
	TagIdType Tag;
	uint8 RootHeap;
	EMemoryTraceHeapAllocationFlags Flags;
};

static_assert(sizeof(FAllocationItem) == 64, "struct FAllocationItem needs packing");

////////////////////////////////////////////////////////////////////////////////////////////////////

class FShortLivingAllocs
{
private:
	struct FNode
	{
		FAllocationItem* Alloc;
		FNode* Next;
		FNode* Prev;
	};

	static const int32 MaxAllocCount = 8 * 1024; // max number of short living allocations

public:
	FShortLivingAllocs();
	~FShortLivingAllocs();

	void Reset();

	bool IsFull() const { return AllocCount == MaxAllocCount; }
	int32 Num() const { return AllocCount; }

	FORCEINLINE FAllocationItem* FindRef(uint64 Address);

	// The collection keeps ownership of FAllocationItem* until Remove is called or until the oldest allocation is removed.
	// Returns the removed oldest allocation if collection is already full; nullptr otherwise.
	// The caller receives ownership of the removed oldest allocation, if a valid pointer is returned.
	FORCEINLINE FAllocationItem* AddAndRemoveOldest(FAllocationItem* Alloc);

	// The caller takes ownership of FAllocationItem*. Returns nullptr if Address is not found.
	FORCEINLINE FAllocationItem* Remove(uint64 Address);

	void Enumerate(TFunctionRef<void(const FAllocationItem& Alloc)> Callback) const;

	// Finds an allocation that contains the address specified or null if not found.
	FAllocationItem* FindRange(uint64 Address) const;

private:
	TMap<uint64, FNode*> AddressMap; // map for short living allocations: Address -> FNode*
	FNode* AllNodes = nullptr; // preallocated array of nodes (MaxAllocCount nodes)
	FNode* LastAddedAllocNode = nullptr; // the last added alloc; double linked list: Prev -> .. -> OldestAlloc
	FNode* OldestAllocNode = nullptr; // the oldest alloc; double linked list: Next -> .. -> LastAddedAlloc
	FNode* FirstUnusedNode = nullptr; // simple linked list with unused nodes (uses Next)
	int32 AllocCount = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FHeapAllocs
{
private:
	struct FNode
	{
		FAllocationItem* Alloc;
		FNode* Next;
		FNode* Prev;
	};

	struct FList
	{
		FNode* First = nullptr;
		FNode* Last = nullptr;
	};

public:
	FHeapAllocs();
	~FHeapAllocs();

	void Reset();

	int32 Num() const { return AllocCount; }

	// Finds the last heap allocation with specified address.
	// Returns the found allocation or nullptr if not found.
	FORCEINLINE FAllocationItem* FindRef(uint64 Address);

	// The collection keeps ownership of FAllocationItem* until Remove is called.
	FORCEINLINE void Add(FAllocationItem* Alloc);

	// The caller takes ownership of FAllocationItem*. Returns nullptr if Address is not found.
	FORCEINLINE FAllocationItem* Remove(uint64 Address);

	void Enumerate(TFunctionRef<void(const FAllocationItem& Alloc)> Callback) const;

	// Finds an allocation that contains the address specified or null if not found.
	FAllocationItem* FindRange(uint64 Address) const;

private:
	TMap<uint64, FList> AddressMap; // map heap allocs: Address -> list of heap allocs
	int32 AllocCount = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FLiveAllocCollection
{
public:
	FLiveAllocCollection();
	~FLiveAllocCollection();

	uint32 Num() const { return TotalAllocCount; }
	uint32 PeakCount() const { return MaxAllocCount; }
	
	// Finds the allocation with specified address.
	// Returns the found allocation or nullptr if not found.
	FORCEINLINE FAllocationItem* FindRef(uint64 Address);

	// Finds the heap allocation with specified address.
	// Returns the found allocation or nullptr if not found.
	FORCEINLINE FAllocationItem* FindHeapRef(uint64 Address);

	// Finds an allocation containing the address.
	// Returns the found allocation or nullptr if not found.
	FORCEINLINE FAllocationItem* FindByAddressRange(uint64 Address);

	// Finds the heap allocation containing the address.
	// Returns the found allocation or nullptr if not found.
	FORCEINLINE FAllocationItem* FindHeapByAddressRange(uint64 Address);

	// Adds a new allocation with specified address.
	// The collection keeps ownership of FAllocationItem* until Remove is called.
	// Returns the new added allocation.
	FORCEINLINE FAllocationItem* AddNew(uint64 Address);

	// Adds a new allocation.
	// The collection keeps ownership of FAllocationItem* until Remove is called.
	FORCEINLINE void Add(FAllocationItem* Alloc);

	// Adds a new heap allocation with specified address.
	// The collection keeps ownership of FAllocationItem* until RemoveHeap is called.
	// Returns the new added allocation.
	FORCEINLINE FAllocationItem* AddNewHeap(uint64 Address);

	// Adds a new heap allocation.
	// The collection keeps ownership of FAllocationItem* until RemoveHeap is called.
	FORCEINLINE void AddHeap(FAllocationItem* HeapAlloc);

	// Removes an allocation with specified address.
	// The caller takes ownership of FAllocationItem*.
	// Returns the removed allocation or nullptr if not found.
	FORCEINLINE FAllocationItem* Remove(uint64 Address);

	// Removes a heap allocation with specified address.
	// The caller takes ownership of FAllocationItem*.
	// Returns the removed heap allocation or nullptr if not found.
	FORCEINLINE FAllocationItem* RemoveHeap(uint64 Address);

	// Enumerates all allocations (including heap allocations).
	void Enumerate(TFunctionRef<void(const FAllocationItem& Alloc)> Callback) const;

private:
	FAllocationItem* LastAlloc = nullptr; // last allocation
	FShortLivingAllocs ShortLivingAllocs; // short living allocs
	TMap<uint64, FAllocationItem*> LongLivingAllocs; // long living allocations
	FHeapAllocs HeapAllocs; // heap allocs

	uint32 TotalAllocCount = 0;
	uint32 MaxAllocCount = 0; // debug stats
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FAllocationsProvider : public IAllocationsProvider
{
private:
	static constexpr double DefaultTimelineSampleGranularity = 0.0001; // 0.1ms

public:
	explicit FAllocationsProvider(IAnalysisSession& InSession);
	virtual ~FAllocationsProvider();

	virtual void BeginEdit() const override { Lock.BeginWrite(); }
	virtual void EndEdit() const override { Lock.EndWrite(); }
	void EditAccessCheck() const { return Lock.WriteAccessCheck(); }

	virtual void BeginRead() const override { Lock.BeginRead(); }
	virtual void EndRead() const override { Lock.EndRead(); }
	void ReadAccessCheck() const { return Lock.ReadAccessCheck(); }

	//////////////////////////////////////////////////
	// Read operations

	virtual bool IsInitialized() const override { ReadAccessCheck(); return bInitialized; }

	virtual uint32 GetTimelineNumPoints() const override { ReadAccessCheck(); return Timeline.Num(); }
	virtual void EnumerateRootHeaps(TFunctionRef<void(HeapId Id, const FHeapSpec&)> Callback) const override;
	virtual void GetTimelineIndexRange(double StartTime, double EndTime, int32& StartIndex, int32& EndIndex) const override;
	virtual void EnumerateMinTotalAllocatedMemoryTimeline(int32 StartIndex, int32 EndIndex, TFunctionRef<void(double Time, double Duration, uint64 Value)> Callback) const override;
	virtual void EnumerateMaxTotalAllocatedMemoryTimeline(int32 StartIndex, int32 EndIndex, TFunctionRef<void(double Time, double Duration, uint64 Value)> Callback) const override;
	virtual void EnumerateMinLiveAllocationsTimeline(int32 StartIndex, int32 EndIndex, TFunctionRef<void(double Time, double Duration, uint32 Value)> Callback) const override;
	virtual void EnumerateMaxLiveAllocationsTimeline(int32 StartIndex, int32 EndIndex, TFunctionRef<void(double Time, double Duration, uint32 Value)> Callback) const override;
	virtual void EnumerateAllocEventsTimeline(int32 StartIndex, int32 EndIndex, TFunctionRef<void(double Time, double Duration, uint32 Value)> Callback) const override;
	virtual void EnumerateFreeEventsTimeline(int32 StartIndex, int32 EndIndex, TFunctionRef<void(double Time, double Duration, uint32 Value)> Callback) const override;

	virtual FQueryHandle StartQuery(const FQueryParams& Params) const override;
	virtual void CancelQuery(FQueryHandle Query) const override;
	virtual const FQueryStatus PollQuery(FQueryHandle Query) const override;

	const FSbTree* GetSbTreeUnchecked(HeapId Heap) const { ReadAccessCheck(); return SbTree[Heap]; }

	void EnumerateLiveAllocs(TFunctionRef<void(const FAllocationItem& Alloc)> Callback) const;
	uint32 GetNumLiveAllocs() const;

	virtual const TCHAR* GetTagName(TagIdType Tag) const { ReadAccessCheck(); return TagTracker.GetTagString(Tag); }
	bool HasTagFromPtrScope(uint32 ThreadId, uint8 Tracker) const { ReadAccessCheck(); return TagTracker.HasTagFromPtrScope(ThreadId, Tracker); }

	void DebugPrint() const;

	//////////////////////////////////////////////////
	// Edit operations

	void EditInit(double Time, uint8 MinAlignment);

	void EditHeapSpec(HeapId Id, HeapId ParentId, const FStringView& Name, EMemoryTraceHeapFlags Flags);

	void EditAlloc(double Time, uint64 Owner, uint64 Address, uint64 Size, uint32 Alignment, uint32 ThreadId, uint8 Tracker, HeapId RootHeap);
	void EditFree(double Time, uint64 Address, HeapId RootHeap);
	void EditMarkAllocationAsHeap(double Time, uint64 Address, HeapId Heap, EMemoryTraceHeapAllocationFlags Flags);
	void EditUnmarkAllocationAsHeap(double Time, uint64 Address, HeapId Heap);
	
	void EditAddTagSpec(TagIdType Tag, TagIdType ParentTag, const TCHAR* Display) { EditAccessCheck(); TagTracker.AddTagSpec(Tag, ParentTag, Display); }
	void EditPushTag(uint32 ThreadId, uint8 Tracker, TagIdType Tag);
	void EditPopTag(uint32 ThreadId, uint8 Tracker);
	void EditPushTagFromPtr(uint32 ThreadId, uint8 Tracker, uint64 Ptr);
	void EditPopTagFromPtr(uint32 ThreadId, uint8 Tracker);

	void EditOnAnalysisCompleted(double Time);

	//////////////////////////////////////////////////

private:
	void UpdateHistogramByAllocSize(uint64 Size);
	void UpdateHistogramByEventDistance(uint32 EventDistance);
	void AdvanceTimelines(double Time);
	void AddHeapSpec(HeapId Id, HeapId ParentId, const FStringView& Name, EMemoryTraceHeapFlags Flags);
	HeapId FindRootHeap(HeapId Heap) const;

private:
	IAnalysisSession& Session;

	mutable FAllocationsProviderLock Lock;

	//Number of supported root heaps
	constexpr static uint8 MaxRootHeaps = 16;
	
	double InitTime = 0;
	uint8 MinAlignment = 0;
	uint8 SizeShift = 0;
	uint8 SummarySizeShift = 0;
	bool bInitialized = false;

	FTagTracker TagTracker;

	uint32 EventIndex[MaxRootHeaps];

	uint64 AllocCount = 0;
	uint64 FreeCount = 0;

	FLiveAllocCollection* LiveAllocs[MaxRootHeaps];

	uint64 MiscErrors = 0;
	uint64 HeapErrors = 0;
	uint64 AllocErrors = 0;
	uint64 FreeErrors = 0;

	uint64 MaxAllocSize = 0;
	uint64 AllocSizeHistogramPow2[65] = { 0 };

	uint32 MaxEventDistance = 0;
	uint32 EventDistanceHistogramPow2[33] = { 0 };

	FSbTree* SbTree[MaxRootHeaps];

	uint64 TotalAllocatedMemory = 0;
	uint32 TotalLiveAllocations = 0;

	double SampleStartTimestamp = 0.0;
	double SampleEndTimestamp = 0.0;
	uint64 SampleMinTotalAllocatedMemory = 0;
	uint64 SampleMaxTotalAllocatedMemory = 0;
	uint32 SampleMinLiveAllocations = 0;
	uint32 SampleMaxLiveAllocations = 0;
	uint32 SampleAllocEvents = 0;
	uint32 SampleFreeEvents = 0;

	TPagedArray<double> Timeline;
	TPagedArray<uint64> MinTotalAllocatedMemoryTimeline;
	TPagedArray<uint64> MaxTotalAllocatedMemoryTimeline;
	TPagedArray<uint32> MinLiveAllocationsTimeline;
	TPagedArray<uint32> MaxLiveAllocationsTimeline;
	TPagedArray<uint32> AllocEventsTimeline;
	TPagedArray<uint32> FreeEventsTimeline;
	
	TArray<FHeapSpec> HeapSpecs;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace TraceServices
