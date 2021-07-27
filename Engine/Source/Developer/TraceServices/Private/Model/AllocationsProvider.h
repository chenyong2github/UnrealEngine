// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Common/PagedArray.h"
#include "Containers/Map.h"
#include "HAL/CriticalSection.h"
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

	struct ThreadState
	{
		TArray<uint32> TagStack;
		bool bReallocTagActive;
	};

	struct TagEntry
	{
		const TCHAR* Display;
		uint32 ParentTag;
	};

public:
	void AddTagSpec(uint32 Tag, uint32 ParentTag, const TCHAR* Display);
	void PushTag(uint32 ThreadId, uint8 Tracker, uint32 Tag);
	void PopTag(uint32 ThreadId, uint8 Tracker);
	uint32 GetCurrentTag(uint32 ThreadId, uint8 Tracker) const;
	const TCHAR* GetTagString(uint32 Tag) const;

	void PushRealloc(uint32 ThreadId, uint8 Tracker, uint32 Tag);
	void PopRealloc(uint32 ThreadId, uint8 Tracker);
	bool HasReallocScope(uint32 ThreadId, uint8 Tracker) const;

private:
	inline uint32 GetTrackerThreadId(uint32 ThreadId, uint8 Tracker) const
	{
		return (Tracker << TrackerIdShift) | (~TrackerIdMask & ThreadId);
	}

	TMap<uint32, ThreadState> TrackerThreadStates;
	TMap<uint32, TagEntry> TagMap;
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

	uint64 GetSize() const { return UnpackSize(SizeAndAlignment); }
	uint32 GetAlignment() const { return UnpackAlignment(SizeAndAlignment); }

	uint32 StartEventIndex;
	uint32 EndEventIndex;
	double StartTime;
	double EndTime;
	uint64 Owner;
	uint64 Address;
	uint64 SizeAndAlignment; // (Alignment << AlignmentShift) | Size
	mutable const FCallstack* Callstack;
	uint32 Tag;
	uint32 Reserved1;
};

static_assert(sizeof(FAllocationItem) == 64, "struct FAllocationItem needs packing");

////////////////////////////////////////////////////////////////////////////////////////////////////
#if 0
struct FAllocationCoreItem
{
	uint32 StartEventIndex;
	uint32 EndEventIndex;
	double StartTime;
	double EndTime;
	uint64 Owner;
	uint64 Base;
	uint32 Size;
};
#endif
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

	bool IsFull() const { return AllocCount == MaxAllocCount; }
	int32 Num() const { return AllocCount; }

	FORCEINLINE FAllocationItem* FindRef(uint64 Address);

	// The collection keeps ownership of FAllocationItem* until Remove is called or until the oldest allocation is removed.
	// Returns the removed oldest allocation if collection is already full; nullptr otherwise.
	// The caller receives ownership of the removed oldest allocation, if a valid pointer is returned.
	FORCEINLINE FAllocationItem* AddChecked(FAllocationItem* Alloc);

	// The caller takes ownership of FAllocationItem*. Returns nullptr if Address is not found.
	FORCEINLINE FAllocationItem* Remove(uint64 Address);

	void Enumerate(TFunctionRef<void(const FAllocationItem& Alloc)> Callback) const;

private:
	TMap<uint64, FNode*> AddressMap; // map for short living allocations: Address -> FNode*
	FNode* AllNodes = nullptr; // preallocated array of nodes
	FNode* LastAddedAllocNode = nullptr; // the last added alloc; double linked list: Prev -> .. -> OldestAlloc
	FNode* OldestAllocNode = nullptr; // the oldest alloc; double linked list: Next -> .. -> LastAddedAlloc
	FNode* FirstUnusedNode = nullptr; // simple linked list with unused nodes (uses Next)
	int32 AllocCount = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FLiveAllocCollection
{
public:
	FLiveAllocCollection();
	~FLiveAllocCollection();

	int32 Num() const { return TotalAllocCount; }
	int32 PeakCount() const { return MaxAllocCount; }

	FORCEINLINE FAllocationItem* FindRef(uint64 Address);

	// The collection keeps ownership of FAllocationItem* until Remove is called.
	// Returns the new added allocation.
	FORCEINLINE FAllocationItem* AddNewChecked(uint64 Address);

	// The caller takes ownership of FAllocationItem*.
	// Returns nullptr if Address is not found.
	FORCEINLINE FAllocationItem* Remove(uint64 Address);

	void Enumerate(TFunctionRef<void(const FAllocationItem& Alloc)> Callback) const;

private:
	FAllocationItem* LastAlloc = nullptr; // last allocation
	FShortLivingAllocs ShortLivingAllocs; // short living allocs
	TMap<uint64, FAllocationItem*> LongLivingAllocs; // long living allocations

	int32 TotalAllocCount = 0;
	int32 MaxAllocCount = 0; // debug stats
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

	const FSbTree* GetSbTree() const { ReadAccessCheck(); return SbTree; }

	void EnumerateLiveAllocs(TFunctionRef<void(const FAllocationItem& Alloc)> Callback) const { ReadAccessCheck(); return LiveAllocs.Enumerate(Callback); }
	uint32 GetNumLiveAllocs() const { ReadAccessCheck(); return LiveAllocs.Num(); }

	bool HasReallocScope(uint32 ThreadId, uint8 Tracker) const { ReadAccessCheck(); return TagTracker.HasReallocScope(ThreadId, Tracker); }

	void DebugPrint() const;

	virtual const TCHAR* GetTagName(int32 Tag) const { ReadAccessCheck(); return TagTracker.GetTagString(Tag); }

	//////////////////////////////////////////////////
	// Edit operations

	void EditInit(double Time, uint8 MinAlignment, uint8 SizeShift, uint8 SummarySizeShift);

	void EditAddCore(double Time, uint64 Owner, uint64 Base, uint32 Size);
	void EditRemoveCore(double Time, uint64 Owner, uint64 Base, uint32 Size);

	void EditAlloc(double Time, uint64 Owner, uint64 Address, uint32 Size, uint8 AlignmentAndSizeLower, uint32 ThreadId, uint8 Tracker);
	void EditFree(double Time, uint64 Address);

	void EditAddTagSpec(int32 Tag, uint32 ParentTag, const TCHAR* Display) { EditAccessCheck(); TagTracker.AddTagSpec(Tag, ParentTag, Display); }
	void EditPushTag(uint32 ThreadId, uint8 Tracker, uint32 Tag)           { EditAccessCheck(); TagTracker.PushTag(ThreadId, Tracker, Tag); }
	void EditPopTag(uint32 ThreadId, uint8 Tracker)                        { EditAccessCheck(); TagTracker.PopTag(ThreadId, Tracker); }

	void EditPushRealloc(uint32 ThreadId, uint8 Tracker, uint64 Ptr);
	void EditPopRealloc(uint32 ThreadId, uint8 Tracker);

	void EditOnAnalysisCompleted(double Time);

	//////////////////////////////////////////////////

private:
	void UpdateHistogramByAllocSize(uint64 Size);
	void UpdateHistogramByEventDistance(uint32 EventDistance);
	void AdvanceTimelines(double Time);

private:
	IAnalysisSession& Session;

	mutable FAllocationsProviderLock Lock;

	double InitTime = 0;
	uint8 MinAlignment = 0;
	uint8 SizeShift = 0;
	uint8 SummarySizeShift = 0;
	bool bInitialized = false;

	FTagTracker TagTracker;

	uint32 EventIndex = 0;

	uint64 AllocCount = 0;
	uint64 FreeCount = 0;

	FLiveAllocCollection LiveAllocs;

	uint64 AllocErrors = 0;
	uint64 FreeErrors = 0;

	uint64 MaxAllocSize = 0;
	uint64 AllocSizeHistogramPow2[65] = { 0 };

	uint32 MaxEventDistance = 0;
	uint32 EventDistanceHistogramPow2[33] = { 0 };

	FSbTree* SbTree;

	uint64 TotalAllocatedMemory = 0;

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
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace TraceServices
