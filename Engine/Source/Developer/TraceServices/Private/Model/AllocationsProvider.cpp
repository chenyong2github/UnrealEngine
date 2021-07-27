// Copyright Epic Games, Inc. All Rights Reserved.

#include "AllocationsProvider.h"

#include "AllocationsQuery.h"
#include "Common/Utils.h"
#include "Containers/ArrayView.h"
#include "SbTree.h"
#include "TraceServices/Containers/Allocators.h"
#include "TraceServices/Model/Callstack.h"

#include <limits>

////////////////////////////////////////////////////////////////////////////////////////////////////

#define INSIGHTS_SLOW_CHECK(expr) //check(expr)

#define INSIGHTS_DEBUG_WATCH 0

// Use optimized path for the short living allocations.
// If enabled, caches the short living allocations, as there is a very high chance to be freed in the next few "free" events.
// ~66% of all allocs are expected to have an "event distance" < 64 events
// ~70% of all allocs are expected to have an "event distance" < 512 events
#define INSIGHTS_USE_SHORT_LIVING_ALLOCS 1

// Use optimized path for the last alloc.
// If enabled, caches the last added alloc, as there is a high chance to be freed in the next "free" event.
// ~10% to ~30% of all allocs are expected to have an "event distance" == 1 event ("free" event follows the "alloc" event immediately)
#define INSIGHTS_USE_LAST_ALLOC 1

#define INSIGHTS_VALIDATE_ALLOC_EVENTS 0

////////////////////////////////////////////////////////////////////////////////////////////////////

namespace TraceServices
{

constexpr uint32 MaxLogMessagesPerErrorType = 100;

#if INSIGHTS_DEBUG_WATCH
static uint64 GWatchAddresses[] =
{
	// add here addresses to watch
	0x0,
};
#endif // INSIGHTS_DEBUG_WATCH

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
// FTagTracker
////////////////////////////////////////////////////////////////////////////////////////////////////

void FTagTracker::AddTagSpec(uint32 InTag, uint32 InParentTag, const TCHAR* InDisplay)
{
	if (ensure(!TagMap.Contains(InTag)))
	{
		TagMap.Emplace(InTag, TagEntry{ InDisplay, InParentTag });
	}
	else
	{
		++NumErrors;
		if (NumErrors <= MaxLogMessagesPerErrorType)
		{
			UE_LOG(LogTraceServices, Error, TEXT("[MemAlloc] Tag with id %u (ParentTag=%u, Display=%s) already added!"), InTag, InParentTag, InDisplay);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTagTracker::PushTag(uint32 InThreadId, uint8 InTracker, uint32 InTag)
{
	const uint32 TrackerThreadId = GetTrackerThreadId(InThreadId, InTracker);
	ThreadState& State = TrackerThreadStates.FindOrAdd(TrackerThreadId);
	State.TagStack.Push(InTag);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTagTracker::PopTag(uint32 InThreadId, uint8 InTracker)
{
	const uint32 TrackerThreadId = GetTrackerThreadId(InThreadId, InTracker);
	ThreadState* State = TrackerThreadStates.Find(TrackerThreadId);
	if (ensure(State && !State->TagStack.IsEmpty()))
	{
		State->TagStack.Pop();
	}
	else
	{
		++NumErrors;
		if (NumErrors <= MaxLogMessagesPerErrorType)
		{
			UE_LOG(LogTraceServices, Error, TEXT("[MemAlloc] Tag stack on Thread %u (Tracker=%u) is already empty!"), InThreadId, InTracker);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint32 FTagTracker::GetCurrentTag(uint32 InThreadId, uint8 InTracker) const
{
	const uint32 TrackerThreadId = GetTrackerThreadId(InThreadId, InTracker);
	const ThreadState* State = TrackerThreadStates.Find(TrackerThreadId);
	if (!State || State->TagStack.IsEmpty())
	{
		return 0; // Untagged
	}
	return State->TagStack.Top();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* FTagTracker::GetTagString(uint32 InTag) const
{
	const TagEntry* Entry = TagMap.Find(InTag);
	return Entry ? Entry->Display : nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTagTracker::PushRealloc(uint32 InThreadId, uint8 InTracker, uint32 InTag)
{
	const uint32 TrackerThreadId = GetTrackerThreadId(InThreadId, InTracker);
	ThreadState& State = TrackerThreadStates.FindOrAdd(TrackerThreadId);
	State.TagStack.Push(InTag);
	State.bReallocTagActive = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTagTracker::PopRealloc(uint32 InThreadId, uint8 InTracker)
{
	const uint32 TrackerThreadId = GetTrackerThreadId(InThreadId, InTracker);
	ThreadState* State = TrackerThreadStates.Find(TrackerThreadId);
	if (ensure(State && !State->TagStack.IsEmpty() && State->bReallocTagActive))
	{
		State->TagStack.Pop();
		State->bReallocTagActive = false;
	}
	else
	{
		++NumErrors;
		if (NumErrors <= MaxLogMessagesPerErrorType)
		{
			UE_LOG(LogTraceServices, Error, TEXT("[MemAlloc] Realloc stack on Thread %u (Tracker=%u) is already empty!"), InThreadId, InTracker);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FTagTracker::HasReallocScope(uint32 InThreadId, uint8 InTracker) const
{
	const uint32 TrackerThreadId = GetTrackerThreadId(InThreadId, InTracker);
	const ThreadState* State = TrackerThreadStates.Find(TrackerThreadId);
	return State && State->bReallocTagActive;
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
const FCallstack* IAllocationsProvider::FAllocation::GetCallstack() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->Callstack;
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
// FShortLivingAllocs
////////////////////////////////////////////////////////////////////////////////////////////////////

FShortLivingAllocs::FShortLivingAllocs()
{
	AllNodes = new FNode[MaxAllocCount];

	// Build the "unused" simple linked list.
	FirstUnusedNode = AllNodes;
	for (int32 Index = 0; Index < MaxAllocCount - 1; ++Index)
	{
		AllNodes[Index].Next = &AllNodes[Index + 1];
	}
	AllNodes[MaxAllocCount - 1].Next = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FShortLivingAllocs::~FShortLivingAllocs()
{
	FNode* Node = LastAddedAllocNode;
	while (Node != nullptr)
	{
		delete Node->Alloc;
		Node = Node->Prev;
	}
	delete[] AllNodes;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FAllocationItem* FShortLivingAllocs::FindRef(uint64 Address)
{
#if 1
	FNode** NodePtr = AddressMap.Find(Address);
	return NodePtr ? (*NodePtr)->Alloc : nullptr;
#else
	// Search linearly the list of allocations, backward, starting with most recent one.
	// As the probability of finding a match for a "free" event is higher for the recent allocs,
	// this could actually be faster than doing a O(log n) search in AddressMap.
	// TODO: This needs to be tested for various MaxAllocCount values.
	FNode* Node = LastAddedAllocNode;
	while (Node != nullptr)
	{
		if (Node->Alloc->Address == Address)
		{
			return Node->Alloc;
		}
		Node = Node->Prev;
	}
	return nullptr;
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FAllocationItem* FShortLivingAllocs::AddChecked(FAllocationItem* Alloc)
{
	if (FirstUnusedNode == nullptr)
	{
		// Collection is already full.
		INSIGHTS_SLOW_CHECK(AllocCount == MaxAllocCount);
		INSIGHTS_SLOW_CHECK(OldestAllocNode != nullptr);
		INSIGHTS_SLOW_CHECK(LastAddedAllocNode != nullptr);

		// Reuse the node of the oldest allocation for the new allocation.
		FNode* NewNode = OldestAllocNode;

		// Remove the oldest allocation.
		FAllocationItem* RemovedAlloc = OldestAllocNode->Alloc;
		AddressMap.Remove(RemovedAlloc->Address);
		OldestAllocNode = OldestAllocNode->Next;
		INSIGHTS_SLOW_CHECK(OldestAllocNode != nullptr);
		OldestAllocNode->Prev = nullptr;

		// Add the new node.
		AddressMap.Add(Alloc->Address, NewNode);
		NewNode->Alloc = Alloc;
		NewNode->Next = nullptr;
		NewNode->Prev = LastAddedAllocNode;
		LastAddedAllocNode->Next = NewNode;
		LastAddedAllocNode = NewNode;

		return RemovedAlloc;
	}
	else
	{
		INSIGHTS_SLOW_CHECK(AllocCount < MaxAllocCount);
		++AllocCount;

		FNode* NewNode = FirstUnusedNode;
		FirstUnusedNode = FirstUnusedNode->Next;

		// Add the new node.
		AddressMap.Add(Alloc->Address, NewNode);
		NewNode->Alloc = Alloc;
		NewNode->Next = nullptr;
		NewNode->Prev = LastAddedAllocNode;
		if (LastAddedAllocNode)
		{
			LastAddedAllocNode->Next = NewNode;
		}
		else
		{
			OldestAllocNode = NewNode;
		}
		LastAddedAllocNode = NewNode;

		return nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FAllocationItem* FShortLivingAllocs::Remove(uint64 Address)
{
	FNode* RemovedNode;
	if (AddressMap.RemoveAndCopyValue(Address, RemovedNode))
	{
		INSIGHTS_SLOW_CHECK(AllocCount > 0);
		--AllocCount;

		FAllocationItem* RemovedAlloc = RemovedNode->Alloc;
		INSIGHTS_SLOW_CHECK(RemovedAlloc->Address == Address);

		// Remove node.
		if (RemovedNode == OldestAllocNode)
		{
			OldestAllocNode = OldestAllocNode->Next;
		}
		if (RemovedNode == LastAddedAllocNode)
		{
			LastAddedAllocNode = LastAddedAllocNode->Prev;
		}
		if (RemovedNode->Prev)
		{
			RemovedNode->Prev->Next = RemovedNode->Next;
		}
		if (RemovedNode->Next)
		{
			RemovedNode->Next->Prev = RemovedNode->Prev;
		}

		// Add the removed node to the "unused" list.
		RemovedNode->Next = FirstUnusedNode;
		FirstUnusedNode = RemovedNode;

		return RemovedAlloc;
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FShortLivingAllocs::Enumerate(TFunctionRef<void(const FAllocationItem& Alloc)> Callback) const
{
	FNode* Node = LastAddedAllocNode;
	while (Node != nullptr)
	{
		Callback(*(Node->Alloc));
		Node = Node->Prev;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FLiveAllocCollection
////////////////////////////////////////////////////////////////////////////////////////////////////

FLiveAllocCollection::FLiveAllocCollection()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLiveAllocCollection::~FLiveAllocCollection()
{
	for (const auto& KV : LongLivingAllocs)
	{
		const FAllocationItem* Allocation = KV.Value;
		delete Allocation;
	}

#if INSIGHTS_USE_LAST_ALLOC
	if (LastAlloc)
	{
		delete LastAlloc;
	}
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FAllocationItem* FLiveAllocCollection::FindRef(uint64 Address)
{
#if INSIGHTS_USE_LAST_ALLOC
	if (LastAlloc && LastAlloc->Address == Address)
	{
		return LastAlloc;
	}
#endif

#if INSIGHTS_USE_SHORT_LIVING_ALLOCS
	FAllocationItem* FoundShortLivingAlloc = ShortLivingAllocs.FindRef(Address);
	if (FoundShortLivingAlloc)
	{
		INSIGHTS_SLOW_CHECK(FoundShortLivingAlloc->Address == Address);
		return FoundShortLivingAlloc;
	}
#endif

	FAllocationItem* FoundLongLivingAlloc = LongLivingAllocs.FindRef(Address);
	if (FoundLongLivingAlloc)
	{
		INSIGHTS_SLOW_CHECK(FoundLongLivingAlloc->Address == Address);
		return FoundLongLivingAlloc;
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FAllocationItem* FLiveAllocCollection::AddNewChecked(uint64 Address)
{
	++TotalAllocCount;
	if (TotalAllocCount > MaxAllocCount)
	{
		MaxAllocCount = TotalAllocCount;
	}

	FAllocationItem* NewAlloc = new FAllocationItem();
	NewAlloc->Address = Address;

	// The allocation that is further moved from "last" to "short living" to "long living".
	FAllocationItem* Allocation = NewAlloc;

#if INSIGHTS_USE_LAST_ALLOC
	if (LastAlloc)
	{
		// We have a new "last allocation".
		// The previous one will be moved to the short living allocations.
		Allocation = LastAlloc;
		LastAlloc = NewAlloc;
	}
	else
	{
		LastAlloc = NewAlloc;
		return NewAlloc;
	}
#endif

#if INSIGHTS_USE_SHORT_LIVING_ALLOCS
	Allocation = ShortLivingAllocs.AddChecked(Allocation);
	if (Allocation == nullptr)
	{
		return NewAlloc;
	}
#endif

	LongLivingAllocs.Add(Allocation->Address, Allocation);
	return NewAlloc;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FAllocationItem* FLiveAllocCollection::Remove(uint64 Address)
{
#if INSIGHTS_USE_LAST_ALLOC
	if (LastAlloc && LastAlloc->Address == Address)
	{
		FAllocationItem* RemovedAlloc = LastAlloc;
		LastAlloc = nullptr;
		INSIGHTS_SLOW_CHECK(TotalAllocCount > 0);
		--TotalAllocCount;
		return RemovedAlloc;
	}
#endif

#if INSIGHTS_USE_SHORT_LIVING_ALLOCS
	FAllocationItem* RemovedShortLivingAlloc = ShortLivingAllocs.Remove(Address);
	if (RemovedShortLivingAlloc)
	{
		INSIGHTS_SLOW_CHECK(RemovedShortLivingAlloc->Address == Address);
		INSIGHTS_SLOW_CHECK(TotalAllocCount > 0);
		--TotalAllocCount;
		return RemovedShortLivingAlloc;
	}
#endif

	FAllocationItem* RemovedLongLivingAlloc;
	if (LongLivingAllocs.RemoveAndCopyValue(Address, RemovedLongLivingAlloc))
	{
		INSIGHTS_SLOW_CHECK(RemovedLongLivingAlloc->Address == Address);
		INSIGHTS_SLOW_CHECK(TotalAllocCount > 0);
		--TotalAllocCount;
		return RemovedLongLivingAlloc;
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLiveAllocCollection::Enumerate(TFunctionRef<void(const FAllocationItem& Alloc)> Callback) const
{
#if INSIGHTS_USE_LAST_ALLOC
	if (LastAlloc)
	{
		Callback(*LastAlloc);
	}
#endif

#if INSIGHTS_USE_SHORT_LIVING_ALLOCS
	ShortLivingAllocs.Enumerate(Callback);
#endif

	for (const auto& KV : LongLivingAllocs)
	{
		const FAllocationItem* Allocation = KV.Value;
		Callback(*Allocation);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FAllocationsProvider
////////////////////////////////////////////////////////////////////////////////////////////////////

FAllocationsProvider::FAllocationsProvider(IAnalysisSession& InSession)
	: Session(InSession)
	, SbTree(nullptr)
	, Timeline(Session.GetLinearAllocator(), 1024)
	, MinTotalAllocatedMemoryTimeline(Session.GetLinearAllocator(), 1024)
	, MaxTotalAllocatedMemoryTimeline(Session.GetLinearAllocator(), 1024)
	, MinLiveAllocationsTimeline(Session.GetLinearAllocator(), 1024)
	, MaxLiveAllocationsTimeline(Session.GetLinearAllocator(), 1024)
	, AllocEventsTimeline(Session.GetLinearAllocator(), 1024)
	, FreeEventsTimeline(Session.GetLinearAllocator(), 1024)
{
	const uint32 ColumnShift = 17; // 1<<17 = 128K
	SbTree = new FSbTree(Session.GetLinearAllocator(), ColumnShift);
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

void FAllocationsProvider::EditAlloc(double Time, uint64 Owner, uint64 Address, uint32 InSize, uint8 InAlignmentAndSizeLower, uint32 ThreadId, uint8 Tracker)
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

	AdvanceTimelines(Time);

	const uint8 SizeLowerMask = ((1 << SizeShift) - 1);
	const uint8 AlignmentMask = ~SizeLowerMask;
	const uint64 Size = (static_cast<uint64>(InSize) << SizeShift) | static_cast<uint64>(InAlignmentAndSizeLower & SizeLowerMask);

	const uint32 Tag = TagTracker.GetCurrentTag(ThreadId, Tracker);

#if INSIGHTS_DEBUG_WATCH
	for (int32 AddrIndex = 0; AddrIndex < UE_ARRAY_COUNT(GWatchAddresses); ++AddrIndex)
	{
		if (GWatchAddresses[AddrIndex] == Address)
		{
			UE_LOG(LogTraceServices, Warning, TEXT("[MemAlloc] Alloc 0x%llX : Size=%llu, Tag=%u, Time=%f"), Address, Size, Tag, Time);
			break;
		}
	}
#endif

#if INSIGHTS_VALIDATE_ALLOC_EVENTS
	FAllocationItem* AllocationPtr = LiveAllocs.FindRef(Address);
#else
	FAllocationItem* AllocationPtr = nullptr;
#endif

	if (!AllocationPtr)
	{
		AllocationPtr = LiveAllocs.AddNewChecked(Address);
		INSIGHTS_SLOW_CHECK(Allocation.Address == Address)

		FAllocationItem& Allocation = *AllocationPtr;
		Allocation.StartEventIndex = EventIndex;
		Allocation.EndEventIndex = (uint32)-1;
		Allocation.StartTime = Time;
		Allocation.EndTime = std::numeric_limits<double>::infinity();
		Allocation.Owner = Owner;
		//Allocation.Address = Address;
		Allocation.SizeAndAlignment = FAllocationItem::PackSizeAndAlignment(Size, InAlignmentAndSizeLower & AlignmentMask);
		Allocation.Callstack = nullptr;
		Allocation.Tag = Tag;
		Allocation.Reserved1 = 0;

		UpdateHistogramByAllocSize(Size);

		// Update stats for the current timeline sample.
		TotalAllocatedMemory += Size;
		SampleMaxTotalAllocatedMemory = FMath::Max(SampleMaxTotalAllocatedMemory, TotalAllocatedMemory);
		SampleMaxLiveAllocations = FMath::Max(SampleMaxLiveAllocations, (uint32)LiveAllocs.Num());
		++SampleAllocEvents;
	}
#if INSIGHTS_VALIDATE_ALLOC_EVENTS
	else
	{
		++AllocErrors;
		if (AllocErrors <= MaxLogMessagesPerErrorType)
		{
			UE_LOG(LogTraceServices, Error, TEXT("[MemAlloc] Invalid ALLOC event (Address=0x%llX, Size=%llu, Tag=%u, Time=%f)!"), Address, Size, Tag, Time);
		}
	}
#endif

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

	AdvanceTimelines(Time);

#if INSIGHTS_DEBUG_WATCH
	for (int32 AddrIndex = 0; AddrIndex < UE_ARRAY_COUNT(GWatchAddresses); ++AddrIndex)
	{
		if (GWatchAddresses[AddrIndex] == Address)
		{
			UE_LOG(LogTraceServices, Warning, TEXT("[MemAlloc] Free 0x%llX : Time=%f"), Address, Time);
			break;
		}
	}
#endif // INSIGHTS_DEBUG_WATCH

	FAllocationItem* AllocationPtr = LiveAllocs.Remove(Address); // we take ownership of AllocationPtr
	if (AllocationPtr)
	{
		check(EventIndex > AllocationPtr->StartEventIndex);
		AllocationPtr->EndEventIndex = EventIndex;
		AllocationPtr->EndTime = Time;

		const uint64 OldSize = AllocationPtr->GetSize();

		SbTree->AddAlloc(AllocationPtr); // SbTree takes ownership of AllocationPtr

		uint32 EventDistance = AllocationPtr->EndEventIndex - AllocationPtr->StartEventIndex;
		UpdateHistogramByEventDistance(EventDistance);

		// Update stats for the current timeline sample.
		TotalAllocatedMemory -= OldSize;
		SampleMinTotalAllocatedMemory = FMath::Min(SampleMinTotalAllocatedMemory, TotalAllocatedMemory);
		SampleMinLiveAllocations = FMath::Min(SampleMinLiveAllocations, (uint32)LiveAllocs.Num());
		++SampleFreeEvents;
	}
	else
	{
		++FreeErrors;
		if (FreeErrors <= MaxLogMessagesPerErrorType)
		{
			UE_LOG(LogTraceServices, Error, TEXT("[MemAlloc] Invalid FREE event (Address=0x%llX, Time=%f)!"), Address, Time);
		}
	}

	++FreeCount;
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
	// If enough time has passed (since the current sample is started)...
	if (Time - SampleStartTimestamp > DefaultTimelineSampleGranularity)
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
		if (Time - SampleEndTimestamp > DefaultTimelineSampleGranularity)
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

void FAllocationsProvider::EditPushRealloc(uint32 ThreadId, uint8 Tracker, uint64 Ptr)
{
	EditAccessCheck();
	int32 Tag(0); // If ptr is not found use "Untagged"
	if (FAllocationItem* Alloc = LiveAllocs.FindRef(Ptr))
	{
		Tag = Alloc->Tag;
	}
	TagTracker.PushRealloc(ThreadId, Tracker, Tag);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::EditPopRealloc(uint32 ThreadId, uint8 Tracker)
{
	EditAccessCheck();
	TagTracker.PopRealloc(ThreadId, Tracker);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::EditOnAnalysisCompleted(double Time)
{
	Lock.WriteAccessCheck();

	if (!bInitialized)
	{
		return;
	}

#if 0
	const bool bResetTimelineAtEnd = false;

	// Add all live allocs to SbTree (with infinite end time).
	uint64 LiveAllocsTotalSize = 0;
	LiveAllocs.Enumerate([](const FAllocationItem& Alloc)
	{
		FAllocationItem* AllocationPtr = const_cast<FAllocationItem*>(&Alloc);

		LiveAllocsTotalSize += AllocationPtr->GetSize();

		// Assign same event index to all live allocs at the end of the session.
		AllocationPtr->EndEventIndex = EventIndex;

		SbTree->AddAlloc(AllocationPtr);

		uint32 EventDistance = AllocationPtr->EndEventIndex - AllocationPtr->StartEventIndex;
		UpdateHistogramByEventDistance(EventDistance);
	});
	//TODO: LiveAllocs.RemoveAll();
	check(TotalAllocatedMemory == LiveAllocsTotalSize);

	if (bResetTimelineAtEnd)
	{
		AdvanceTimelines(Time + 10 * DefaultTimelineSampleGranularity);

		const uint32 LiveAllocsTotalCount = (uint32)LiveAllocs.Num();
		LiveAllocs.Empty();

		// Update stats for the last timeline sample (reset to zero).
		TotalAllocatedMemory = 0;
		SampleMinTotalAllocatedMemory = 0;
		SampleMinLiveAllocations = 0;
		SampleFreeEvents += LiveAllocsTotalCount;
	}
#endif

	// Flush the last cached timeline sample.
	AdvanceTimelines(std::numeric_limits<double>::infinity());

#if 0
	DebugPrint();
#endif

	SbTree->Validate();
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
	const FCallstacksProvider* CallstacksProvider = Session.ReadProvider<FCallstacksProvider>(FName("CallstacksProvider"));
	auto* Inner = new FAllocationsQuery(*this, *CallstacksProvider, Params);
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

FName GetAllocationsProviderName()
{
	static FName Name(TEXT("AllocationsProvider"));
	return Name;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const IAllocationsProvider* ReadAllocationsProvider(const IAnalysisSession& Session)
{
	return Session.ReadProvider<IAllocationsProvider>(GetAllocationsProviderName());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace TraceServices

#undef INSIGHTS_SLOW_CHECK
#undef INSIGHTS_DEBUG_WATCH
#undef INSIGHTS_USE_SHORT_LIVING_ALLOCS
#undef INSIGHTS_USE_LAST_ALLOC
#undef INSIGHTS_VALIDATE_ALLOC_EVENTS
