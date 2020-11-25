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

uint32 IAllocationsProvider::FAllocation::GetSize() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->Size;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint8 IAllocationsProvider::FAllocation::GetAlignment() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->Alignment;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint8 IAllocationsProvider::FAllocation::GetWaste() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->Waste;
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

void FAllocationsProvider::EditInit(double InTime, uint8 InMinAlignment, uint8 InSizeShift, uint8 InSummarySizeShift, uint8 InMode)
{
	if (bInitialized)
	{
		// error: already initialized
		return;
	}

	InitTime = InTime;
	MinAlignment = InMinAlignment;
	SizeShift = InSizeShift;
	SummarySizeShift = InSummarySizeShift;
	Mode = InMode;

	bInitialized = true;
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

void FAllocationsProvider::EditAlloc(double Time, uint64 Owner, uint64 Address, uint32 Size, uint8 Alignment, uint8 Waste, uint32 Tag)
{
	Lock.WriteAccessCheck();

	if (!bInitialized)
	{
		return;
	}

	FAllocationItem* AllocationPtr = LiveAllocs.Find(Address);
	if (AllocationPtr)
	{
		++AllocErrors;
	}
	else
	{
		SbTree->SetTimeForEvent(EventIndex, Time);

		FAllocationItem Allocation =
		{
			EventIndex,
			(uint32)-1,
			Time,
			std::numeric_limits<double>::infinity(),
			Owner,
			Address,
			Size,
			Alignment,
			Waste,
			static_cast<uint16>(Tag),
			0, // Reserverd1
			0, // Reserverd2
		};
		LiveAllocs.Add(Address, Allocation);

		//TODO: Cache the last added alloc, as there is ~10% chance to be freed in the next event.

		if (LiveAllocs.Num() > MaxLiveAllocCount)
		{
			MaxLiveAllocCount = LiveAllocs.Num();
		}

		UpdateHistogramByAllocSize(Size, Alignment, Waste);
	}

	++AllocCount;
	++EventIndex;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::EditRealloc(double Time, uint64 Owner, uint64 FreeAddress, uint64 Address, uint32 Size, uint8 Alignment, uint8 Waste)
{
	Lock.WriteAccessCheck();

	if (!bInitialized)
	{
		return;
	}

	SbTree->SetTimeForEvent(EventIndex, Time);

	FAllocationItem* AllocationPtr = LiveAllocs.Find(FreeAddress);
	if (!AllocationPtr)
	{
		++ReallocErrors;
	}
	else
	{
		check(EventIndex > AllocationPtr->StartEventIndex);
		AllocationPtr->EndEventIndex = EventIndex;
		AllocationPtr->EndTime = Time;

		SbTree->AddAlloc(AllocationPtr);

		uint32 EventDistance = AllocationPtr->EndEventIndex - AllocationPtr->StartEventIndex;
		UpdateHistogramByEventDistance(EventDistance);

		if (FreeAddress == Address)
		{
			AllocationPtr->StartEventIndex = EventIndex;
			AllocationPtr->EndEventIndex = -1;
			AllocationPtr->StartTime = Time;
			AllocationPtr->EndTime = std::numeric_limits<double>::infinity();
			AllocationPtr->Owner = Owner;
			AllocationPtr->Address = Address;
			AllocationPtr->Size = Size;
			AllocationPtr->Alignment = Alignment;
			AllocationPtr->Waste = Waste;
		}
		else
		{
			uint16 Tag = AllocationPtr->Tag;
			LiveAllocs.Remove(FreeAddress);

			FAllocationItem Allocation =
			{
				EventIndex,
				(uint32)-1,
				Time,
				std::numeric_limits<double>::infinity(),
				Owner,
				Address,
				Size,
				Alignment,
				Waste,
				Tag,
				0, // Reserverd1
				0, // Reserverd2
			};
			LiveAllocs.Add(Address, Allocation);
		}

		UpdateHistogramByAllocSize(Size, Alignment, Waste);
	}

	++AllocCount;
	++EventIndex;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::EditFree(double Time, uint64 Owner, uint64 FreeAddress)
{
	Lock.WriteAccessCheck();

	if (!bInitialized)
	{
		return;
	}

	SbTree->SetTimeForEvent(EventIndex, Time);

	FAllocationItem* AllocationPtr = LiveAllocs.Find(FreeAddress);
	if (!AllocationPtr)
	{
		++FreeErrors;
	}
	else
	{
		check(EventIndex > AllocationPtr->StartEventIndex);
		AllocationPtr->EndEventIndex = EventIndex;
		AllocationPtr->EndTime = Time;

		SbTree->AddAlloc(AllocationPtr);

		uint32 EventDistance = AllocationPtr->EndEventIndex - AllocationPtr->StartEventIndex;
		UpdateHistogramByEventDistance(EventDistance);

		LiveAllocs.Remove(FreeAddress);
	}

	++EventIndex;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FAllocationsProvider::UpdateHistogramByAllocSize(uint32 Size, uint8 Alignment, uint8 Waste)
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
	// 32 : [2^31 .. 2^32 - 1]
	uint32 HistogramIndexPow2 = 32 - FMath::CountLeadingZeros(Size);
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
	// 0 : [0 .. 1]
	// 1 : [2 .. 3]
	// 2 : [4 .. 7]
	// 3 : [8 .. 15]
	// ...
	// i : [2^i .. 2^(i+1)-1]
	// ...
	// 63 : [2^63 .. 2^64-1]
	uint32 HistogramIndexPow2 = (EventDistance > 0) ? 63 - FMath::CountLeadingZeros64(EventDistance) : 0;
	++EventDistanceHistogramPow2[HistogramIndexPow2];
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
