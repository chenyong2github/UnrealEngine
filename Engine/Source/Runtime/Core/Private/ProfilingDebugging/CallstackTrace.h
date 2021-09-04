// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "Experimental/Containers/GrowOnlyLockFreeHash.h"
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

		FCallstackTracer(FMalloc* InMalloc)
			: KnownSet(InMalloc)
		{
			KnownSet.Reserve(1024 * 1024 * 2);
		}
		
		void AddCallstack(const FBacktraceEntry& Entry)
		{
			bool bAlreadyAdded = false;

			// Our set implementation doesn't allow for zero entries (zero represents an empty element
			// in the hash table), so if we get one due to really bad luck in our 64-bit Id calculation,
			// treat it as a "1" instead, for purposes of tracking if we've seen that callstack.
			KnownSet.FindOrAdd(FMath::Max(Entry.Id, (uint64)1), true, &bAlreadyAdded);
			if (!bAlreadyAdded)
			{
				UE_TRACE_LOG(Memory, CallstackSpec, CallstackChannel)
					<< CallstackSpec.Id(Entry.Id)
					<< CallstackSpec.Frames(Entry.Frames, Entry.FrameCount);
			}
		}
	private:
		struct FEncounteredCallstackSetEntry
		{
			std::atomic_uint64_t Data;

			inline uint64 GetKey() const { return Data.load(std::memory_order_relaxed); }
			inline bool GetValue() const { return true; }
			inline bool IsEmpty() const { return Data.load(std::memory_order_relaxed) == 0; }
			inline void SetKeyValue(uint64 Key, bool Value) { Data.store(Key, std::memory_order_relaxed); }
			static inline uint32 KeyHash(uint64 Key) { return static_cast<uint32>(Key); }
			static inline void ClearEntries(FEncounteredCallstackSetEntry* Entries, int32 EntryCount)
			{
				memset(Entries, 0, EntryCount * sizeof(FEncounteredCallstackSetEntry));
			}
		};
		typedef TGrowOnlyLockFreeHash<FEncounteredCallstackSetEntry, uint64, bool> FEncounteredCallstackSet;

		FEncounteredCallstackSet 	KnownSet;
	};
}
