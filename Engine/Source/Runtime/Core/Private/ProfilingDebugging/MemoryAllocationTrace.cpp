// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProfilingDebugging/MemoryAllocationTrace.h"
#include "HAL/PlatformTime.h"

#if UE_MEMORY_TRACE_ENABLED

////////////////////////////////////////////////////////////////////////////////
namespace UE {
namespace Trace {

TRACELOG_API void Update();

} // namespace Trace
} // namespace UE

////////////////////////////////////////////////////////////////////////////////
UE_TRACE_CHANNEL(MemSummaryChannel)
UE_TRACE_CHANNEL_DEFINE(MemAllocChannel)

UE_TRACE_EVENT_BEGIN(Memory, Init, NoSync|Important)
	UE_TRACE_EVENT_FIELD(uint32, MarkerPeriod)
	UE_TRACE_EVENT_FIELD(uint8, MinAlignment)
	UE_TRACE_EVENT_FIELD(uint8, SizeShift)
	UE_TRACE_EVENT_FIELD(uint8, Mode)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Memory, Marker)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Memory, CoreAdd)
	UE_TRACE_EVENT_FIELD(uint64, Owner)
	UE_TRACE_EVENT_FIELD(void*, Base)
	UE_TRACE_EVENT_FIELD(uint32, Size)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Memory, CoreRemove)
	UE_TRACE_EVENT_FIELD(uint64, Owner)
	UE_TRACE_EVENT_FIELD(void*, Base)
	UE_TRACE_EVENT_FIELD(uint32, Size)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Memory, Alloc)
	UE_TRACE_EVENT_FIELD(uint64, Owner)
	UE_TRACE_EVENT_FIELD(void*, Address)
	UE_TRACE_EVENT_FIELD(uint32, Size)
	UE_TRACE_EVENT_FIELD(uint8, Alignment_SizeLower)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Memory, Free)
	UE_TRACE_EVENT_FIELD(void*, Address)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Memory, ReallocAlloc)
	UE_TRACE_EVENT_FIELD(uint64, Owner)
	UE_TRACE_EVENT_FIELD(void*, Address)
	UE_TRACE_EVENT_FIELD(uint32, Size)
	UE_TRACE_EVENT_FIELD(uint8, Alignment_SizeLower)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Memory, ReallocFree)
	UE_TRACE_EVENT_FIELD(void*, Address)
UE_TRACE_EVENT_END()


////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::Initialize()
{
	UE_TRACE_LOG(Memory, Init, MemAllocChannel)
		<< Init.MarkerPeriod(MarkerSamplePeriod + 1)
		<< Init.MinAlignment(uint8(MIN_ALIGNMENT))
		<< Init.SizeShift(uint8(SizeShift));

	static_assert((1 << SizeShift) - 1 <= MIN_ALIGNMENT, "Not enough bits to pack size fields");
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::EnableTracePump()
{
	bPumpTrace = true;
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::Update()
{
	uint32 TheCount = MarkerCounter.fetch_add(1, std::memory_order_relaxed);
	if ((TheCount & MarkerSamplePeriod) == 0)
	{
		UE_TRACE_LOG(Memory, Marker, MemAllocChannel)
			<< Marker.Cycle(FPlatformTime::Cycles64());
	}

	if (bPumpTrace)
	{
		UE::Trace::Update();
	}
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::CoreAdd(void* Base, size_t Size, void* Owner)
{
	UE_TRACE_LOG(Memory, CoreAdd, MemAllocChannel)
		<< CoreAdd.Owner(uint64(Owner))
		<< CoreAdd.Base(Base)
		<< CoreAdd.Size(uint32(Size >> SizeShift));

	Update();
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::CoreRemove(void* Base, size_t Size, void* Owner)
{
	UE_TRACE_LOG(Memory, CoreRemove, MemAllocChannel)
		<< CoreRemove.Owner(uint64(Owner))
		<< CoreRemove.Base(Base)
		<< CoreRemove.Size(uint32(Size >> SizeShift));

	Update();
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::Alloc(void* Address, size_t Size, uint32 Alignment, void* Owner)
{
	uint32 Alignment_SizeLower = Alignment | (Size & ((1 << SizeShift) - 1));

	UE_TRACE_LOG(Memory, Alloc, MemAllocChannel)
		<< Alloc.Owner(uint64(Owner))
		<< Alloc.Address(Address)
		<< Alloc.Size(uint32(Size >> SizeShift))
		<< Alloc.Alignment_SizeLower(uint8(Alignment_SizeLower));

	Update();
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::Free(void* Address)
{
	UE_TRACE_LOG(Memory, Free, MemAllocChannel)
		<< Free.Address(Address);

	Update();
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::ReallocAlloc(void* Address, size_t Size, uint32 Alignment, void* Owner)
{
	uint32 Alignment_SizeLower = Alignment | (Size & ((1 << SizeShift) - 1));

	UE_TRACE_LOG(Memory, ReallocAlloc, MemAllocChannel)
		<< ReallocAlloc.Owner(uint64(Owner))
		<< ReallocAlloc.Address(Address)
		<< ReallocAlloc.Size(uint32(Size >> SizeShift))
		<< ReallocAlloc.Alignment_SizeLower(uint8(Alignment_SizeLower));

	Update();
}

////////////////////////////////////////////////////////////////////////////////
void FAllocationTrace::ReallocFree(void* Address)
{
	UE_TRACE_LOG(Memory, ReallocFree, MemAllocChannel)
		<< ReallocFree.Address(Address);

	Update();
}

#endif // UE_MEMORY_TRACE_ENABLED
