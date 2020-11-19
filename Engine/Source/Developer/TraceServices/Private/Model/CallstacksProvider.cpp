// Copyright Epic Games, Inc. All Rights Reserved.

#include "CallstacksProvider.h"
#include "Misc/ScopeRWLock.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "Algo/Unique.h"
#include "Containers/ArrayView.h"
#include "UObject/NameTypes.h"

namespace TraceServices
{

/////////////////////////////////////////////////////////////////////
#ifdef TRACE_CALLSTACK_STATS
static struct FCallstackProviderStats
{
	uint64		Callstacks;
	uint64		Frames;
	uint64		FrameCountHistogram[256];
} GCallstackStats;
#endif

/////////////////////////////////////////////////////////////////////
FCallstacksProvider::FCallstacksProvider(IAnalysisSession& Session)
	: Frames(Session.GetLinearAllocator(), FramesPerPage) 
{
}

/////////////////////////////////////////////////////////////////////
void FCallstacksProvider::AddCallstack(uint64 InCallstackId, const uint64* InFrames, uint8 InFrameCount)
{
	if (InFrameCount == 0)
	{
		return;
	}

#ifdef TRACE_CALLSTACK_STATS
	GCallstackStats.Callstacks++;
	GCallstackStats.Frames += InFrameCount;
	GCallstackStats.FrameCountHistogram[InFrameCount]++;
#endif

	// Make sure all the frames fit on one page by appending dummy entries.
	const uint64 PageHeadroom = Frames.GetPageSize() - (Frames.Num() % Frames.GetPageSize());
	if (PageHeadroom < InFrameCount)
	{
		FRWScopeLock WriteLock(EntriesLock, SLT_Write);
		uint64 EntriesToAdd = PageHeadroom + 1; // Fill page and allocate one on next
		do { Frames.PushBack(); } while (--EntriesToAdd);
	}

	// Append the incoming frames
	const uint64 FirstFrame = Frames.Num();
	for (uint32 FrameIdx = 0; FrameIdx < InFrameCount; ++FrameIdx)
	{
		FStackFrame& F = Frames.PushBack();
		F.Addr = InFrames[FrameIdx];
		F.Symbol = nullptr;
	}

	uint64 FrameIdxAndLen = (uint64(InFrameCount) << EntryLenShift) | (~EntryLenMask & FirstFrame);

	{
		FRWScopeLock WriteLock(EntriesLock, SLT_Write);
		CallstackEntries.Add(InCallstackId, FCallstackEntry{ FrameIdxAndLen });
	}
}

/////////////////////////////////////////////////////////////////////
FCallstack FCallstacksProvider::GetCallstack(uint64 CallstackId) 
{
	FRWScopeLock ReadLock(EntriesLock, SLT_ReadOnly);
	FCallstackEntry* Entry = CallstackEntries.Find(CallstackId);
	if (Entry)
	{
		const uint64 FirstFrameIdx = ~EntryLenMask & Entry->CallstackLenIndex;
		const uint8 FrameCount = Entry->CallstackLenIndex >> EntryLenShift;
		return FCallstack(&Frames[FirstFrameIdx], FrameCount);
	}
	return FCallstack(nullptr, 0);	
}

/////////////////////////////////////////////////////////////////////
void FCallstacksProvider::GetCallstacks(const TArrayView<uint64>& CallstackIds, FCallstack* OutCallstacks)
{
	uint64 OutIdx(0);
	check(OutCallstacks != nullptr);

	FRWScopeLock ReadLock(EntriesLock, SLT_ReadOnly);
	for (uint64 CallstackId : CallstackIds)
	{
		FCallstackEntry* Entry = CallstackEntries.Find(CallstackId);
		if (Entry)
		{
			const uint64 FirstFrameIdx = ~EntryLenMask & Entry->CallstackLenIndex;
			const uint8 FrameCount = Entry->CallstackLenIndex >> EntryLenShift;
			new(&OutCallstacks[OutIdx]) FCallstack(&Frames[FirstFrameIdx], FrameCount);
		}
		else
		{
			new(&OutCallstacks[OutIdx]) FCallstack(nullptr, 0);	
		}
		OutIdx++;
	}
}

/////////////////////////////////////////////////////////////////////
FName FCallstacksProvider::GetName() const
{
	static FName Name(TEXT("CallstacksProvider"));
	return Name;
}

} // namespace TraceServices
