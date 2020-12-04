// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Common/PagedArray.h"
#include "Containers/Map.h"
#include "HAL/CriticalSection.h"
#include "TraceServices/Model/AllocationsProvider.h"

namespace TraceServices
{

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
	uint32 Tag;
	uint32 Reserved1;
	uint64 Reserved2;
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

class FAllocationsProvider : public IAllocationsProvider
{
public:
	FAllocationsProvider(ILinearAllocator& InAllocator);
	virtual ~FAllocationsProvider();

	static FName GetName();

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

	void DebugPrint() const;

	//////////////////////////////////////////////////
	// Edit operations

	void EditInit(double Time, uint8 MinAlignment, uint8 SizeShift, uint8 SummarySizeShift);
	void EditAddCore(double Time, uint64 Owner, uint64 Base, uint32 Size);
	void EditRemoveCore(double Time, uint64 Owner, uint64 Base, uint32 Size);
	void EditAlloc(double Time, uint64 Owner, uint64 Address, uint32 Size, uint8 AlignmentAndSizeLower, uint32 Tag);
	void EditFree(double Time, uint64 Address);

	void EditOnAnalysisCompleted();

	//////////////////////////////////////////////////

private:
	void UpdateHistogramByAllocSize(uint64 Size);
	void UpdateHistogramByEventDistance(uint32 EventDistance);
	void AdvanceTimelines(double Time);

private:
	ILinearAllocator& Allocator;

	mutable FAllocationsProviderLock Lock;

	double InitTime = 0;
	uint8 MinAlignment = 0;
	uint8 SizeShift = 0;
	uint8 SummarySizeShift = 0;
	bool bInitialized = false;

	uint32 EventIndex = 0;

	uint64 AllocCount = 0;
	uint64 FreeCount = 0;

	uint32 MaxLiveAllocCount = 0;
	TMap<uint64, FAllocationItem> LiveAllocs;

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
