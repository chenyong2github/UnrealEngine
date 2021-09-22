// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/Callstack.h"
#include "HAL/CriticalSection.h"
#include "Common/PagedArray.h"
#include "Containers/Map.h"

namespace TraceServices
{

class IAnalysisSession;
class IModuleProvider;

/////////////////////////////////////////////////////////////////////
class FCallstacksProvider : public ICallstacksProvider
{
public:
	explicit FCallstacksProvider(IAnalysisSession& Session);
	virtual ~FCallstacksProvider() {}

	const FCallstack*	GetCallstack(uint64 CallstackId) const override;
	void				GetCallstacks(const TArrayView<uint64>& CallstackIds, FCallstack const** OutCallstacks) const override;
	void				AddCallstack(uint64 CallstackId, const uint64* Frames, uint8 FrameCount);
	void				AddCallstack(uint32 CallstackId, const uint64* Frames, uint8 FrameCount);

private:
	enum
	{
		FramesPerPage		= 65536, // 16 bytes/entry -> 1 Mb per page
		CallstacksPerPage	= 65536 * 2 // 8 bytes/callstack -> 1Mb per page
	};

	mutable FRWLock					EntriesLock;
	IAnalysisSession&				Session;
	mutable IModuleProvider*		ModuleProvider;
	TMap<uint64,const FCallstack*>	CallstackEntries;
	TPagedArray<FCallstack>			Callstacks;
	TPagedArray<FStackFrame>		Frames;
};

} // namespace TraceServices
