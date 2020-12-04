// Copyright Epic Games, Inc. All Rights Reserved.

#include "AllocationsProvider.h"

#include "AllocationsQuery.h"
#include "SbTree.h"
#include "TraceServices/Containers/Allocators.h"

#include <limits>

namespace TraceServices
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FAllocationsProviderLock
////////////////////////////////////////////////////////////////////////////////////////////////////

thread_local FAllocationsProviderLock* GThreadCurrentAllocationsProviderLock;
thread_local int32 GThreadCurrentReadAllocationsProviderLockCount;
thread_local int32 GThreadCurrentWriteAllocationsProviderLockCount;

void FAllocationsProviderLock::ReadAccessCheck() const
{
	checkf(GThreadCurrentAllocationsProviderLock == this && (GThreadCurrentReadAllocationsProviderLockCount > 0 || GThreadCurrentWriteAllocationsProviderLockCount > 0),
		TEXT("Trying to READ from allocations provider outside of a READ scope"));
}

void FAllocationsProviderLock::WriteAccessCheck() const
{
	checkf(GThreadCurrentAllocationsProviderLock == this && GThreadCurrentWriteAllocationsProviderLockCount > 0,
		TEXT("Trying to WRITE to allocations provider outside of an EDIT/WRITE scope"));
}

void FAllocationsProviderLock::BeginRead()
{
	check(!GThreadCurrentAllocationsProviderLock || GThreadCurrentAllocationsProviderLock == this);
	checkf(GThreadCurrentWriteAllocationsProviderLockCount == 0, TEXT("Trying to lock allocations provider for READ while holding EDIT/WRITE access"));
	if (GThreadCurrentReadAllocationsProviderLockCount++ == 0)
	{
		GThreadCurrentAllocationsProviderLock = this;
		RWLock.ReadLock();
	}
}

void FAllocationsProviderLock::EndRead()
{
	check(GThreadCurrentReadAllocationsProviderLockCount > 0);
	if (--GThreadCurrentReadAllocationsProviderLockCount == 0)
	{
		RWLock.ReadUnlock();
		GThreadCurrentAllocationsProviderLock = nullptr;
	}
}

void FAllocationsProviderLock::BeginWrite()
{
	check(!GThreadCurrentAllocationsProviderLock || GThreadCurrentAllocationsProviderLock == this);
	checkf(GThreadCurrentReadAllocationsProviderLockCount == 0, TEXT("Trying to lock allocations provider for EDIT/WRITE while holding READ access"));
	if (GThreadCurrentWriteAllocationsProviderLockCount++ == 0)
	{
		GThreadCurrentAllocationsProviderLock = this;
		RWLock.WriteLock();
	}
}

void FAllocationsProviderLock::EndWrite()
{
	check(GThreadCurrentWriteAllocationsProviderLockCount > 0);
	if (--GThreadCurrentWriteAllocationsProviderLockCount == 0)
	{
		RWLock.WriteUnlock();
		GThreadCurrentAllocationsProviderLock = nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// IAllocationsProvider::FAllocation
////////////////////////////////////////////////////////////////////////////////////////////////////

double IAllocationsProvider::FAllocation::GetStartTime() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->StartTime;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

double IAllocationsProvider::FAllocation::GetEndTime() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->EndTime;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint64 IAllocationsProvider::FAllocation::GetAddress() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->Address;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint64 IAllocationsProvider::FAllocation::GetSize() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->GetSize();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint32 IAllocationsProvider::FAllocation::GetAlignment() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->GetAlignment();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint64 IAllocationsProvider::FAllocation::GetBacktraceId() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->Owner;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint32 IAllocationsProvider::FAllocation::GetTag() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->Tag;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// IAllocationsProvider::FAllocations
////////////////////////////////////////////////////////////////////////////////////////////////////

void IAllocationsProvider::FAllocations::operator delete (void* Address)
{
	auto* Inner = (const FAllocationsImpl*)Address;
	delete Inner;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint32 IAllocationsProvider::FAllocations::Num() const
{
	auto* Inner = (const FAllocationsImpl*)this;
	return (uint32)(Inner->Items.Num());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const IAllocationsProvider::FAllocation* IAllocationsProvider::FAllocations::Get(uint32 Index) const
{
	checkSlow(Index < Num());

	auto* Inner = (const FAllocationsImpl*)this;
	return (const FAllocation*)(Inner->Items[Index]);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// IAllocationsProvider::FQueryStatus
////////////////////////////////////////////////////////////////////////////////////////////////////

IAllocationsProvider::FQueryResult IAllocationsProvider::FQueryStatus::NextResult() const
{
	auto* Inner = (FAllocationsImpl*)Handle;
	if (Inner == nullptr)
	{
		return nullptr;
	}

	Handle = UPTRINT(Inner->Next);

	auto* Ret = (FAllocations*)Inner;
	return FQueryResult(Ret);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FAllocationsProvider
////////////////////////////////////////////////////////////////////////////////////////////////////

FAllocationsProvider::FAllocationsProvider(ILinearAllocator& InAllocator)
	: Allocator(InAllocator)
	, SbTree(nullptr)
	, Timeline(InAllocator, 1024)
	, MinTotalAllocatedMemoryTimeline(InAllocator, 1024)
	, MaxTotalAllocatedMemoryTimeline(InAllocator, 1024)
	, MinLiveAllocationsTimeline(InAllocator, 1024)
	, MaxLiveAllocationsTimeline(InAllocator, 1024)
	, AllocEventsTimeline(InAllocator, 1024)
	, FreeEventsTimeline(InAllocator, 1024)
{
	const uint32 ColumnShift = 17; // 1<<17 = 128K
	SbTree = new FSbTree(InAllocator, ColumnShift);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FAllocationsProvider::~FAllocationsProvider()
{
	if (SbTree)
	{
		delete SbTree;
		SbTree = nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FName FAllocationsProvider::GetName()
{
	static FName Name(TEXT("AllocationsProvider"));
	return Name;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::EditInit(double InTime, uint8 InMinAlignment, uint8 InSizeShift, uint8 InSummarySizeShift)
{
	Lock.WriteAccessCheck();

	if (bInitialized)
	{
		// error: already initialized
		return;
	}

	InitTime = InTime;
	MinAlignment = InMinAlignment;
	SizeShift = InSizeShift;
	SummarySizeShift = InSummarySizeShift;

	bInitialized = true;

	AdvanceTimelines(InTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::EditAddCore(double Time, uint64 Owner, uint64 Base, uint32 Size)
{
	Lock.WriteAccessCheck();

	if (!bInitialized)
	{
		return;
	}

	//TODO
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::EditRemoveCore(double Time, uint64 Owner, uint64 Base, uint32 Size)
{
	Lock.WriteAccessCheck();

	if (!bInitialized)
	{
		return;
	}

	//TODO
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::EditAlloc(double Time, uint64 Owner, uint64 Address, uint32 InSize, uint8 InAlignmentAndSizeLower, uint32 Tag)
{
	Lock.WriteAccessCheck();

	if (!bInitialized)
	{
		return;
	}

	if (Address == 0)
	{
		return;
	}

	SbTree->SetTimeForEvent(EventIndex, Time);

	FAllocationItem* AllocationPtr = LiveAllocs.Find(Address);
	if (AllocationPtr)
	{
		++AllocErrors;
	}
	else
	{
		AdvanceTimelines(Time);

		const uint8 SizeLowerMask = ((1 << SizeShift) - 1);
		const uint8 AlignmentMask = ~SizeLowerMask;

		const uint64 Size = (static_cast<uint64>(InSize) << SizeShift) | static_cast<uint64>(InAlignmentAndSizeLower & SizeLowerMask);

		FAllocationItem Allocation =
		{
			EventIndex,
			(uint32)-1,
			Time,
			std::numeric_limits<double>::infinity(),
			Owner,
			Address,
			FAllocationItem::PackSizeAndAlignment(Size, InAlignmentAndSizeLower & AlignmentMask),
			Tag,
			0, // Reserverd1
			0, // Reserverd2
		};
		LiveAllocs.Add(Address, Allocation);

		//TODO: Cache the last added alloc, as there is ~10% chance to be freed in the next event.

		const uint32 CurrentLiveAllocCount = static_cast<uint32>(LiveAllocs.Num());
		if (CurrentLiveAllocCount > MaxLiveAllocCount)
		{
			MaxLiveAllocCount = CurrentLiveAllocCount;
		}

		UpdateHistogramByAllocSize(Size);

		// Update stats for current timeline sample.
		TotalAllocatedMemory += Size;
		SampleMaxTotalAllocatedMemory = FMath::Max(SampleMaxTotalAllocatedMemory, TotalAllocatedMemory);
		SampleMaxLiveAllocations = FMath::Max(SampleMaxLiveAllocations, (uint32)LiveAllocs.Num());
		++SampleAllocEvents;
	}

	++AllocCount;
	++EventIndex;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::EditFree(double Time, uint64 Address)
{
	Lock.WriteAccessCheck();

	if (!bInitialized)
	{
		return;
	}

	if (Address == 0)
	{
		return;
	}

	SbTree->SetTimeForEvent(EventIndex, Time);

	FAllocationItem* AllocationPtr = LiveAllocs.Find(Address);
	if (!AllocationPtr)
	{
		++FreeErrors;
	}
	else
	{
		AdvanceTimelines(Time);

		check(EventIndex > AllocationPtr->StartEventIndex);
		AllocationPtr->EndEventIndex = EventIndex;
		AllocationPtr->EndTime = Time;

		const uint64 OldSize = AllocationPtr->GetSize();

		SbTree->AddAlloc(AllocationPtr);

		uint32 EventDistance = AllocationPtr->EndEventIndex - AllocationPtr->StartEventIndex;
		UpdateHistogramByEventDistance(EventDistance);

		LiveAllocs.Remove(Address);

		// Update stats for current timeline sample.
		TotalAllocatedMemory -= OldSize;
		SampleMinTotalAllocatedMemory = FMath::Min(SampleMinTotalAllocatedMemory, TotalAllocatedMemory);
		SampleMinLiveAllocations = FMath::Min(SampleMinLiveAllocations, (uint32)LiveAllocs.Num());
		++SampleFreeEvents;
	}

	++EventIndex;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::UpdateHistogramByAllocSize(uint64 Size)
{
	if (Size > MaxAllocSize)
	{
		MaxAllocSize = Size;
	}

	// HistogramIndex : Value Range
	// 0 : [0]
	// 1 : [1]
	// 2 : [2 .. 3]
	// 3 : [4 .. 7]
	// ...
	// i : [2^(i-1) .. 2^i-1], i > 0
	// ...
	// 64 : [2^63 .. 2^64-1]
	uint32 HistogramIndexPow2 = 64 - FMath::CountLeadingZeros64(Size);
	++AllocSizeHistogramPow2[HistogramIndexPow2];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::UpdateHistogramByEventDistance(uint32 EventDistance)
{
	if (EventDistance > MaxEventDistance)
	{
		MaxEventDistance = EventDistance;
	}

	// HistogramIndex : Value Range
	// 0 : [0]
	// 1 : [1]
	// 2 : [2 .. 3]
	// 3 : [4 .. 7]
	// ...
	// i : [2^(i-1) .. 2^i-1], i > 0
	// ...
	// 32 : [2^31 .. 2^32-1]
	uint32 HistogramIndexPow2 = 32 - FMath::CountLeadingZeros(EventDistance);
	++EventDistanceHistogramPow2[HistogramIndexPow2];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::AdvanceTimelines(double Time)
{
	constexpr double SampleGranularity = 0.001; // 1ms

	// If enough time has passed (since the current sample is started)...
	if (Time - SampleStartTimestamp > SampleGranularity)
	{
		// Add the current sample to the timelines.
		Timeline.EmplaceBack(SampleStartTimestamp);
		MinTotalAllocatedMemoryTimeline.EmplaceBack(SampleMinTotalAllocatedMemory);
		MaxTotalAllocatedMemoryTimeline.EmplaceBack(SampleMaxTotalAllocatedMemory);
		MinLiveAllocationsTimeline.EmplaceBack(SampleMinLiveAllocations);
		MaxLiveAllocationsTimeline.EmplaceBack(SampleMaxLiveAllocations);
		AllocEventsTimeline.EmplaceBack(SampleAllocEvents);
		FreeEventsTimeline.EmplaceBack(SampleFreeEvents);

		// Start a new sample.
		SampleStartTimestamp = Time;
		const uint32 NumLiveAllocs = (uint32)LiveAllocs.Num();
		SampleMinTotalAllocatedMemory = TotalAllocatedMemory;
		SampleMaxTotalAllocatedMemory = TotalAllocatedMemory;
		SampleMinLiveAllocations = NumLiveAllocs;
		SampleMaxLiveAllocations = NumLiveAllocs;
		SampleAllocEvents = 0;
		SampleFreeEvents = 0;

		// If the previous sample is well distanced in time...
		if (Time - SampleEndTimestamp > SampleGranularity) 
		{
			// Add an intermediate "flat region" sample.
			Timeline.EmplaceBack(SampleEndTimestamp);
			MinTotalAllocatedMemoryTimeline.EmplaceBack(TotalAllocatedMemory);
			MaxTotalAllocatedMemoryTimeline.EmplaceBack(TotalAllocatedMemory);
			MinLiveAllocationsTimeline.EmplaceBack(NumLiveAllocs);
			MaxLiveAllocationsTimeline.EmplaceBack(NumLiveAllocs);
			AllocEventsTimeline.EmplaceBack(0);
			FreeEventsTimeline.EmplaceBack(0);
		}
	}

	SampleEndTimestamp = Time;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::EditOnAnalysisCompleted()
{
	Lock.WriteAccessCheck();

	if (!bInitialized)
	{
		return;
	}

	AdvanceTimelines(std::numeric_limits<double>::infinity());

	//DebugPrint();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::GetTimelineIndexRange(double StartTime, double EndTime, int32& StartIndex, int32& EndIndex) const
{
	using PageType = const TPagedArrayPage<double>;

	PageType* PageData = Timeline.GetPages();
	if (PageData)
	{
		const int32 NumPoints = static_cast<int32>(Timeline.Num());

		const int32 NumPages = static_cast<int32>(Timeline.NumPages());
		TArrayView<PageType, int32> Pages(PageData, NumPages);

		const int32 StartPageIndex = Algo::UpperBoundBy(Pages, StartTime, [](PageType& Page) { return Page.Items[0]; }) - 1;
		if (StartPageIndex < 0)
		{
			StartIndex = -1;
		}
		else
		{
			PageType& Page = PageData[StartPageIndex];
			TArrayView<double> PageValues(Page.Items, static_cast<int32>(Page.Count));
			const int32 Index = Algo::UpperBound(PageValues, StartTime) - 1;
			check(Index >= 0);
			StartIndex = StartPageIndex * static_cast<int32>(Timeline.GetPageSize()) + Index;
			check(Index < NumPoints);
		}

		const int32 EndPageIndex = Algo::UpperBoundBy(Pages, EndTime, [](PageType& Page) { return Page.Items[0]; }) - 1;
		if (EndPageIndex < 0)
		{
			EndIndex = -1;
		}
		else
		{
			PageType& Page = PageData[EndPageIndex];
			TArrayView<double> PageValues(Page.Items, static_cast<int32>(Page.Count));
			const int32 Index = Algo::UpperBound(PageValues, EndTime) - 1;
			check(Index >= 0);
			EndIndex = EndPageIndex * static_cast<int32>(Timeline.GetPageSize()) + Index;
			check(Index < NumPoints);
		}
	}
	else
	{
		StartIndex = -1;
		EndIndex = -1;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::EnumerateMinTotalAllocatedMemoryTimeline(int32 StartIndex, int32 EndIndex, TFunctionRef<void(double Time, double Duration, uint64 Value)> Callback) const
{
	const int32 NumPoints = Timeline.Num();
	StartIndex = FMath::Max(StartIndex, 0);
	EndIndex = FMath::Min(EndIndex + 1, NumPoints); // make it exclusive
	if (StartIndex < EndIndex)
	{
		auto TimeIt = Timeline.GetIteratorFromItem(StartIndex);
		auto ValueIt = MinTotalAllocatedMemoryTimeline.GetIteratorFromItem(StartIndex);
		double PrevTime = *TimeIt;
		uint64 PrevValue = *ValueIt;
		++TimeIt;
		++ValueIt;
		for (int32 Index = StartIndex + 1; Index < EndIndex; ++Index, ++TimeIt, ++ValueIt)
		{
			const double Time = *TimeIt;
			Callback(PrevTime, Time - PrevTime, PrevValue);
			PrevTime = *TimeIt;
			PrevValue = *ValueIt;
		}
		if (EndIndex < NumPoints)
		{
			const double Time = *TimeIt;
			Callback(PrevTime, Time - PrevTime, PrevValue);
		}
		else
		{
			Callback(PrevTime, std::numeric_limits<double>::infinity(), PrevValue);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::EnumerateMaxTotalAllocatedMemoryTimeline(int32 StartIndex, int32 EndIndex, TFunctionRef<void(double Time, double Duration, uint64 Value)> Callback) const
{
	const int32 NumPoints = Timeline.Num();
	StartIndex = FMath::Max(StartIndex, 0);
	EndIndex = FMath::Min(EndIndex + 1, NumPoints); // make it exclusive
	if (StartIndex < EndIndex)
	{
		auto TimeIt = Timeline.GetIteratorFromItem(StartIndex);
		auto ValueIt = MaxTotalAllocatedMemoryTimeline.GetIteratorFromItem(StartIndex);
		double PrevTime = *TimeIt;
		uint64 PrevValue = *ValueIt;
		++TimeIt;
		++ValueIt;
		for (int32 Index = StartIndex + 1; Index < EndIndex; ++Index, ++TimeIt, ++ValueIt)
		{
			const double Time = *TimeIt;
			Callback(PrevTime, Time - PrevTime, PrevValue);
			PrevTime = *TimeIt;
			PrevValue = *ValueIt;
		}
		if (EndIndex < NumPoints)
		{
			const double Time = *TimeIt;
			Callback(PrevTime, Time - PrevTime, PrevValue);
		}
		else
		{
			Callback(PrevTime, std::numeric_limits<double>::infinity(), PrevValue);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::EnumerateMinLiveAllocationsTimeline(int32 StartIndex, int32 EndIndex, TFunctionRef<void(double Time, double Duration, uint32 Value)> Callback) const
{
	const int32 NumPoints = Timeline.Num();
	StartIndex = FMath::Max(StartIndex, 0);
	EndIndex = FMath::Min(EndIndex + 1, NumPoints); // make it exclusive
	if (StartIndex < EndIndex)
	{
		auto TimeIt = Timeline.GetIteratorFromItem(StartIndex);
		auto ValueIt = MinLiveAllocationsTimeline.GetIteratorFromItem(StartIndex);
		double PrevTime = *TimeIt;
		uint32 PrevValue = *ValueIt;
		++TimeIt;
		++ValueIt;
		for (int32 Index = StartIndex + 1; Index < EndIndex; ++Index, ++TimeIt, ++ValueIt)
		{
			const double Time = *TimeIt;
			Callback(PrevTime, Time - PrevTime, PrevValue);
			PrevTime = *TimeIt;
			PrevValue = *ValueIt;
		}
		if (EndIndex < NumPoints)
		{
			const double Time = *TimeIt;
			Callback(PrevTime, Time - PrevTime, PrevValue);
		}
		else
		{
			Callback(PrevTime, std::numeric_limits<double>::infinity(), PrevValue);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::EnumerateMaxLiveAllocationsTimeline(int32 StartIndex, int32 EndIndex, TFunctionRef<void(double Time, double Duration, uint32 Value)> Callback) const
{
	const int32 NumPoints = Timeline.Num();
	StartIndex = FMath::Max(StartIndex, 0);
	EndIndex = FMath::Min(EndIndex + 1, NumPoints); // make it exclusive
	if (StartIndex < EndIndex)
	{
		auto TimeIt = Timeline.GetIteratorFromItem(StartIndex);
		auto ValueIt = MaxLiveAllocationsTimeline.GetIteratorFromItem(StartIndex);
		double PrevTime = *TimeIt;
		uint32 PrevValue = *ValueIt;
		++TimeIt;
		++ValueIt;
		for (int32 Index = StartIndex + 1; Index < EndIndex; ++Index, ++TimeIt, ++ValueIt)
		{
			const double Time = *TimeIt;
			Callback(PrevTime, Time - PrevTime, PrevValue);
			PrevTime = *TimeIt;
			PrevValue = *ValueIt;
		}
		if (EndIndex < NumPoints)
		{
			const double Time = *TimeIt;
			Callback(PrevTime, Time - PrevTime, PrevValue);
		}
		else
		{
			Callback(PrevTime, std::numeric_limits<double>::infinity(), PrevValue);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::EnumerateAllocEventsTimeline(int32 StartIndex, int32 EndIndex, TFunctionRef<void(double Time, double Duration, uint32 Value)> Callback) const
{
	const int32 NumPoints = Timeline.Num();
	StartIndex = FMath::Max(StartIndex, 0);
	EndIndex = FMath::Min(EndIndex + 1, NumPoints); // make it exclusive
	if (StartIndex < EndIndex)
	{
		auto TimeIt = Timeline.GetIteratorFromItem(StartIndex);
		auto ValueIt = AllocEventsTimeline.GetIteratorFromItem(StartIndex);
		double PrevTime = *TimeIt;
		uint32 PrevValue = *ValueIt;
		++TimeIt;
		++ValueIt;
		for (int32 Index = StartIndex + 1; Index < EndIndex; ++Index, ++TimeIt, ++ValueIt)
		{
			const double Time = *TimeIt;
			Callback(PrevTime, Time - PrevTime, PrevValue);
			PrevTime = *TimeIt;
			PrevValue = *ValueIt;
		}
		if (EndIndex < NumPoints)
		{
			const double Time = *TimeIt;
			Callback(PrevTime, Time - PrevTime, PrevValue);
		}
		else
		{
			Callback(PrevTime, std::numeric_limits<double>::infinity(), PrevValue);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::EnumerateFreeEventsTimeline(int32 StartIndex, int32 EndIndex, TFunctionRef<void(double Time, double Duration, uint32 Value)> Callback) const
{
	const int32 NumPoints = Timeline.Num();
	StartIndex = FMath::Max(StartIndex, 0);
	EndIndex = FMath::Min(EndIndex + 1, NumPoints); // make it exclusive
	if (StartIndex < EndIndex)
	{
		auto TimeIt = Timeline.GetIteratorFromItem(StartIndex);
		auto ValueIt = FreeEventsTimeline.GetIteratorFromItem(StartIndex);
		double PrevTime = *TimeIt;
		uint32 PrevValue = *ValueIt;
		++TimeIt;
		++ValueIt;
		for (int32 Index = StartIndex + 1; Index < EndIndex; ++Index, ++TimeIt, ++ValueIt)
		{
			const double Time = *TimeIt;
			Callback(PrevTime, Time - PrevTime, PrevValue);
			PrevTime = *TimeIt;
			PrevValue = *ValueIt;
		}
		if (EndIndex < NumPoints)
		{
			const double Time = *TimeIt;
			Callback(PrevTime, Time - PrevTime, PrevValue);
		}
		else
		{
			Callback(PrevTime, std::numeric_limits<double>::infinity(), PrevValue);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::DebugPrint() const
{
	SbTree->DebugPrint();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

IAllocationsProvider::FQueryHandle FAllocationsProvider::StartQuery(const IAllocationsProvider::FQueryParams& Params) const
{
	auto* Inner = new FAllocationsQuery(*this, Params);
	return IAllocationsProvider::FQueryHandle(Inner);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::CancelQuery(FQueryHandle Query) const
{
	auto* Inner = (FAllocationsQuery*)Query;
	return Inner->Cancel();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const IAllocationsProvider::FQueryStatus FAllocationsProvider::PollQuery(FQueryHandle Query) const
{
	auto* Inner = (FAllocationsQuery*)Query;
	return Inner->Poll();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const IAllocationsProvider* ReadAllocationsProvider(const IAnalysisSession& Session)
{
	return Session.ReadProvider<IAllocationsProvider>(FAllocationsProvider::GetName());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace TraceServices
