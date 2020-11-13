// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/Callstack.h"
#include "HAL/CriticalSection.h"
#include "Common/PagedArray.h"

namespace TraceServices
{

class IAnalysisSession;

/////////////////////////////////////////////////////////////////////
class FCallstacksProvider : public ICallstacksProvider
{
public:
					FCallstacksProvider(IAnalysisSession& Session);
	virtual			~FCallstacksProvider() {}

	FCallstack		GetCallstack(uint64 CallstackId) override;
	void			GetCallstacks(const TArrayView<uint64>& CallstackIds, FCallstack* OutCallstacks) override;
	void			AddCallstack(uint64 CallstackId, const uint64* Frames, uint8 FrameCount);
	FName			GetName() const;

private:
	struct FCallstackEntry
	{
		uint64 CallstackLenIndex;
	};

	enum
	{
		FramesPerPage	= 65536, // 16 bytes/entry -> 1 Mb per page
	};

	enum : uint64
	{
		EntryLenMask	= 0xff00000000000000,
		EntryLenShift	= 56,
	};


	FRWLock							EntriesLock;
	TMap<uint64, FCallstackEntry>	CallstackEntries;
	TPagedArray<FStackFrame>		Frames;
};

} // namespace TraceServices
