// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Experimental/Containers/SherwoodHashTable.h"
#include "HAL/CriticalSection.h"
#include "Trace/Trace.h"
#include "Trace/Trace.inl"
#include "Misc/ScopeLock.h"

////////////////////////////////////////////////////////////////////////////////
UE_TRACE_CHANNEL_EXTERN(CallstackChannel)

UE_TRACE_EVENT_BEGIN_EXTERN(Memory, CallstackSpec, NoSync)
	UE_TRACE_EVENT_FIELD(uint64, Id)
	UE_TRACE_EVENT_FIELD(uint64[], Frames)
UE_TRACE_EVENT_END()

////////////////////////////////////////////////////////////////////////////////
namespace {

	class FCallstackTracer
	{
	public:
		struct FBacktraceEntry
		{
			enum {MaxStackDepth = 256};
			uint64	Id = 0;
			uint32	FrameCount = 0;
			uint64	Frames[MaxStackDepth] = { 0 };
		};

		FCallstackTracer()
		{
			KnownSet.Reserve(1024 * 1024 * 2);
		}
		
		void AddCallstack(const FBacktraceEntry& Entry)
		{
			FScopeLock _(&ProducerCs);
			bool bAlreadyAdded = false;
			KnownSet.Add(Entry.Id, &bAlreadyAdded);
			if (!bAlreadyAdded)
			{
				UE_TRACE_LOG(Memory, CallstackSpec, CallstackChannel)
					<< CallstackSpec.Id(Entry.Id)
					<< CallstackSpec.Frames(Entry.Frames, Entry.FrameCount);
			}
		}
	private:	
		Experimental::TSherwoodSet<uint64> 	KnownSet;
		FCriticalSection					ProducerCs;
	};
}
