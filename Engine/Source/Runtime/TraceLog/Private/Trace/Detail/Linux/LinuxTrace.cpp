// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Trace/Trace.h"

#if UE_TRACE_ENABLED

#include "Trace/Detail/PosixTrace.inl"

#include <sys/mman.h>
#include <time.h>

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
uint8* MemoryReserve(SIZE_T Size)
{
	return (uint8*)mmap(nullptr, Size, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
}

////////////////////////////////////////////////////////////////////////////////
void MemoryFree(void* Address, SIZE_T Size)
{
	munmap(Address, Size);
}

////////////////////////////////////////////////////////////////////////////////
void MemoryMap(void* Address, SIZE_T Size)
{
	mprotect(Address, Size, PROT_READ|PROT_WRITE);
}

////////////////////////////////////////////////////////////////////////////////
void MemoryUnmap(void* Address, SIZE_T Size)
{
	madvise(Address, Size, MADV_REMOVE);
}



////////////////////////////////////////////////////////////////////////////////
uint64 TimeGetFrequency()
{
	return uint64(1000 * 1000 * 1000);
}

////////////////////////////////////////////////////////////////////////////////
uint64 TimeGetTimestamp()
{
	timespec Time;
	clock_gettime(CLOCK_MONOTONIC_RAW, &Time);
	return (uint64(Time.tv_sec) * (1000 * 1000 * 1000)) + Time.tv_nsec;
}

} // namespace Trace

#endif // UE_TRACE_ENABLED
