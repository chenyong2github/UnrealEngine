// Copyright Epic Games, Inc. All Rights Reserved.

#include "CallstacksProvider.h"
#include "Misc/ScopeRWLock.h"
#include "ModuleProvider.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "Algo/Unique.h"
#include "Containers/ArrayView.h"
#include "UObject/NameTypes.h"

namespace TraceServices
{

/////////////////////////////////////////////////////////////////////
static const FResolvedSymbol GNeverResolveSymbol(ESymbolQueryResult::NotLoaded, nullptr, nullptr, nullptr, 0);
static const FResolvedSymbol GNotFoundSymbol(ESymbolQueryResult::NotFound, TEXT("Unknown"), nullptr, nullptr, 0);
static const FResolvedSymbol GNoSymbol(ESymbolQueryResult::NotFound, TEXT("No callstack recorded"), nullptr, nullptr, 0);
static constexpr FStackFrame GNotFoundStackFrame = { 0, &GNotFoundSymbol};
static constexpr FStackFrame GNoStackFrame = {0, &GNoSymbol};	
static const FCallstack GNotFoundCallstack(&GNotFoundStackFrame, 1);
static const FCallstack GNoCallstack(&GNoStackFrame, 1);

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
FCallstacksProvider::FCallstacksProvider(IAnalysisSession& InSession)
	: Session(InSession)
	, ModuleProvider(nullptr)
	, Callstacks(InSession.GetLinearAllocator(), CallstacksPerPage)
	, Frames(InSession.GetLinearAllocator(), FramesPerPage)
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

	// The module provider is created on the fly so we want to cache it 
	// once it's available. Note that the module provider is conditionally
	// created so EditProvider() may return a null pointer.
	if (!ModuleProvider)
	{
		ModuleProvider = Session.EditProvider<IModuleProvider>(GetModuleProviderName());
	}

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
		
		if (ModuleProvider)
		{
			// This will return immediately. The result will be empty if the symbol 
			// has not been encountered before, and resolution has been queued up.
			F.Symbol = ModuleProvider->GetSymbol(InFrames[FrameIdx]);
		}
		else
		{
			F.Symbol = &GNeverResolveSymbol;
		}
	}

	{
		FRWScopeLock WriteLock(EntriesLock, SLT_Write);
		FCallstack* Callstack = &Callstacks.EmplaceBack(&Frames[FirstFrame], InFrameCount);
		CallstackEntries.Add(InCallstackId, Callstack);
	}
}

/////////////////////////////////////////////////////////////////////
void FCallstacksProvider::AddCallstack(uint32 InCallstackId, const uint64* InFrames, uint8 InFrameCount)
{
	// In order to maintain backward compatibility to older format where we sent the runtime hash value
	// we keep the callstack entry key as uint64.
	AddCallstack(uint64(InCallstackId), InFrames, InFrameCount);
}

/////////////////////////////////////////////////////////////////////
const FCallstack* FCallstacksProvider::GetCallstack(uint64 CallstackId) const
{
	FRWScopeLock ReadLock(EntriesLock, SLT_ReadOnly);
	if (CallstackId == 0)
	{
		return &GNoCallstack;
	}
	const FCallstack* const* FindResult = CallstackEntries.Find(CallstackId);
	return FindResult ? *FindResult : &GNotFoundCallstack;
}

/////////////////////////////////////////////////////////////////////
void FCallstacksProvider::GetCallstacks(const TArrayView<uint64>& CallstackIds, FCallstack const** OutCallstacks) const
{
	uint64 OutIdx(0);
	check(OutCallstacks != nullptr);

	FRWScopeLock ReadLock(EntriesLock, SLT_ReadOnly);
	for (uint64 CallstackId : CallstackIds)
	{
		const FCallstack* const* FindResult = CallstackEntries.Find(CallstackId);
		OutCallstacks[OutIdx] = FindResult ? *FindResult : &GNotFoundCallstack;
		OutIdx++;
	}
}

/////////////////////////////////////////////////////////////////////
FName GetCallstacksProviderName()
{
	static FName Name(TEXT("CallstacksProvider"));
	return Name;
}

const ICallstacksProvider* ReadCallstacksProvider(const IAnalysisSession& Session)
{
	return Session.ReadProvider<ICallstacksProvider>(GetCallstacksProviderName());
}

} // namespace TraceServices
