// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Memory.h"

namespace TraceServices
{

/////////////////////////////////////////////////////////////////////
struct SymbolEntry
{
	TCHAR*			Name;
	TCHAR*			Filename;
	TCHAR*			Module;
	uint32			Line;
};

/////////////////////////////////////////////////////////////////////
struct FStackFrame
{
	uint64			Addr;
	SymbolEntry*	Symbol;
};

/////////////////////////////////////////////////////////////////////
struct FCallstack
{
					FCallstack(FStackFrame* FirstEntry, uint8 FrameCount);
	/** Get the number of stack frames in callstack. */
	uint32			Num();
	/** Gets the address at a given stack depth. */
	uint64			Addr(uint8 Depth);
	/** Gets the cached symbol name at a given stack depth. */
	TCHAR*			SymbolName(uint8 Depth);

private:
	FStackFrame*	FirstEntry;
	uint8			FrameCount;
};

/////////////////////////////////////////////////////////////////////
class ICallstacksProvider
	: public IProvider
{
public:
	virtual				~ICallstacksProvider() = default;

	/**
	  * Queries a callstack id.
	  * @param CallstackId		Callstack id to query
	  * @return					Callstack information. If id is not found a callstack with zero stack depth is returned.
	  */
	virtual FCallstack	GetCallstack(uint64 CallstackId) = 0;

	/**
	  * Queries a set of callstack ids.
	  * @param CallstackIds		List of callstack ids to query
	  * @param OutCallstacks	Output list of callstacks. Caller is responsible for allocating space according to CallstackIds length.
	  */
	virtual void		GetCallstacks(const TArrayView<uint64>& CallstackIds, FCallstack* OutCallstacks) = 0;
};

/////////////////////////////////////////////////////////////////////
inline FCallstack::FCallstack(FStackFrame* InFirstEntry, uint8 InFrameCount)
	: FirstEntry(InFirstEntry)
	, FrameCount(InFrameCount)
{
}

/////////////////////////////////////////////////////////////////////
inline uint32 FCallstack::Num()
{
	return FrameCount;
}

/////////////////////////////////////////////////////////////////////
inline uint64 FCallstack::Addr(uint8 Depth)
{
	return Depth < FrameCount ? FirstEntry[Depth].Addr : 0;
}

/////////////////////////////////////////////////////////////////////
inline TCHAR* FCallstack::SymbolName(uint8 Depth)
{
	return Depth < FrameCount ? FirstEntry[Depth].Symbol->Name : 0;
}

} // namespace TraceServices
