// Copyright Epic Games, Inc. All Rights Reserved.

#include "AllocationsProvider.h"

#if defined(UE_USE_ALLOCATIONS_PROVIDER)

////////////////////////////////////////////////////////////////////////////////
struct FAllocationItem
{
	double	StartTime;
	double	EndTime;
	uint64	Address;
	uint64	Size;
	uint64	BacktraceId;
	uint16	Tag;
};

////////////////////////////////////////////////////////////////////////////////
double IAllocationsProvider::FAllocation::GetStartTime() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->StartTime;
}

////////////////////////////////////////////////////////////////////////////////
double IAllocationsProvider::FAllocation::GetEndTime() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->EndTime;
}

////////////////////////////////////////////////////////////////////////////////
uint64 IAllocationsProvider::FAllocation::GetAddress() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->Address;
}

////////////////////////////////////////////////////////////////////////////////
uint64 IAllocationsProvider::FAllocation::GetSize() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->Size;
}

////////////////////////////////////////////////////////////////////////////////
uint64 IAllocationsProvider::FAllocation::GetBacktraceId() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->BacktraceId;
}

////////////////////////////////////////////////////////////////////////////////
uint32 IAllocationsProvider::FAllocation::GetTag() const
{
	const auto* Inner = (const FAllocationItem*)this;
	return Inner->Tag;
}



////////////////////////////////////////////////////////////////////////////////
struct FAllocationsImpl
{
	void*				operator new (SIZE_T, uint32 NumItems);
	FAllocationsImpl*	Next;
	uint32				NumItems;
	FAllocationItem		Items[];
};

////////////////////////////////////////////////////////////////////////////////
void* FAllocationsImpl::operator new (SIZE_T Size, uint32 NumItems)
{
	Size += (NumItems * sizeof(Items[0]));
	return FMemory::Malloc(Size, alignof(FAllocationsImpl));
}



////////////////////////////////////////////////////////////////////////////////
void IAllocationsProvider::FAllocations::operator delete (void* Address)
{
	FMemory::Free(Address);
}

////////////////////////////////////////////////////////////////////////////////
uint32 IAllocationsProvider::FAllocations::Num() const
{
	auto* Inner = (FAllocationsImpl*)this;
	return Inner->NumItems;
}

////////////////////////////////////////////////////////////////////////////////
const IAllocationsProvider::FAllocation* IAllocationsProvider::FAllocations::Get(uint32 Index) const
{
	if (Index >= Num())
	{
		return nullptr;
	}

	auto* Inner = (FAllocationsImpl*)this;
	return (FAllocation*)(Inner->Items + Index);
}


namespace TEMP
{

////////////////////////////////////////////////////////////////////////////////
using ECrosses				= IAllocationsProvider::ECrosses;
using EQueryStatus			= IAllocationsProvider::EQueryStatus;
using FQueryStatus			= IAllocationsProvider::FQueryStatus;
using FAllocation			= IAllocationsProvider::FAllocation;
static const uint64 Mlcg	= 0x106689d45497fdb5ull;

////////////////////////////////////////////////////////////////////////////////
class FMockQuery
{
public:
						FMockQuery(double TimeA, double TimeB, ECrosses Crosses);
	void				Cancel() { delete this; }
	FQueryStatus		Poll();

private:
	bool				NextBlock();
	uint64				NextSeed(uint64 Pow2Max=0);
	uint64				Seed;
	uint32				BlockIndex;
	uint32				BlockEnd;
	FAllocationsImpl*	BlockList;
};

////////////////////////////////////////////////////////////////////////////////
FMockQuery::FMockQuery(double TimeA, double TimeB, ECrosses Crosses)
{
	BlockIndex = uint32(TimeA / 5.0);
	BlockEnd = uint32(TimeB / 5.0) + 1;
	BlockList = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
bool FMockQuery::NextBlock()
{
	if (BlockIndex >= BlockEnd)
	{
		return false;
	}

	Seed = (BlockIndex + 1) * Mlcg;

	// Build a set of results
	FAllocationsImpl* ResultList = nullptr;
	uint32 NumResults = (NextSeed(32) - 16) + 32;
	for (uint32 i = 0; i < NumResults; ++i)
	{
		// How many allocations shall we put in this result page?
		uint32 NumAllocs = NextSeed(1 << 20);
		FAllocationsImpl* Result = new (NumAllocs) FAllocationsImpl();
		for (uint32 j = 0; j < NumAllocs; ++j)
		{
			uint64 Address = 0x200000000000 | (NextSeed(1ull << 40) & ~0x0f);
			double EndTime = double(BlockIndex * 5.0);

			FAllocationItem& Allocation = Result->Items[j];
			Allocation.StartTime = (EndTime * double(NextSeed(2048)) / 2048.0);
			Allocation.EndTime = EndTime;
			Allocation.Address = Address;
			Allocation.Size = (NextSeed(512) - 256) + 768;
			Allocation.BacktraceId = NextSeed(1ull << 48);
			Allocation.Tag = NextSeed(4);
		}

		Result->NumItems = NumAllocs;
		Result->Next = ResultList;
		ResultList = Result;
	}

	BlockList = ResultList;

	++BlockIndex;
	return true;
}

////////////////////////////////////////////////////////////////////////////////
uint64 FMockQuery::NextSeed(uint64 Pow2Max)
{
	Seed *= Mlcg;
	return (Seed >> 8) & (Pow2Max - 1);
}

////////////////////////////////////////////////////////////////////////////////
FQueryStatus FMockQuery::Poll()
{
	FQueryStatus Status = {};
	Status.Handle = 0;

	// Is there nothing more to do?
	if (BlockList == nullptr && !NextBlock())
	{
		Status.Status = EQueryStatus::Done;
		return Status;
	}

	// Simulate there not being any results yet.
	bool bNotReady = (NextSeed(8) > 6);
	if (bNotReady)
	{
		Status.Status = EQueryStatus::Working;
		return Status;
	}

	// Slice a bit off the linked list.
	FAllocationsImpl* Head = BlockList;
	FAllocationsImpl* Tail = BlockList;
	for (uint32 i = NextSeed(32); i-- && Tail->Next != nullptr;)
	{
		Tail = Tail->Next;
	}
	BlockList = Tail->Next;
	Tail->Next = nullptr;

	Status.Status = EQueryStatus::Available;
	Status.Handle = UPTRINT(Head);
	return Status;
}

} // namespace TEMP



////////////////////////////////////////////////////////////////////////////////
IAllocationsProvider::QueryResult IAllocationsProvider::FQueryStatus::NextResult() const
{
	auto* Inner = (FAllocationsImpl*)Handle;
	if (Inner == nullptr)
	{
		return nullptr;
	}

	Handle = UPTRINT(Inner->Next);

	auto* Ret = (FAllocations*)Inner;
	return QueryResult(Ret);
}



////////////////////////////////////////////////////////////////////////////////
FAllocationsProvider::FAllocationsProvider()
{
}

////////////////////////////////////////////////////////////////////////////////
FAllocationsProvider::~FAllocationsProvider()
{
}

////////////////////////////////////////////////////////////////////////////////
FName FAllocationsProvider::GetName() const
{
	static FName Name(TEXT("AllocationsProvider"));
	return Name;
}

////////////////////////////////////////////////////////////////////////////////
IAllocationsProvider::QueryHandle FAllocationsProvider::StartQuery(
	double TimeA,
	double TimeB,
	ECrosses Crosses)
{
	auto* Inner = new TEMP::FMockQuery(TimeA, TimeB, Crosses);
	return IAllocationsProvider::QueryHandle(Inner);
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationsProvider::CancelQuery(QueryHandle Query)
{
	auto* Inner = (TEMP::FMockQuery*)Query;
	return Inner->Cancel();
}

////////////////////////////////////////////////////////////////////////////////
IAllocationsProvider::QueryStatus FAllocationsProvider::PollQuery(QueryHandle Query)
{
	auto* Inner = (TEMP::FMockQuery*)Query;
	return Inner->Poll();
}

#endif // UE_USE_ALLOCATIONS_PROVIDER
