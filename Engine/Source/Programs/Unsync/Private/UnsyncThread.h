// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UnsyncUtil.h"

UNSYNC_THIRD_PARTY_INCLUDES_START
#include <atomic>
#if UNSYNC_USE_CONCRT
#	include <concrt.h>
#	include <concurrent_queue.h>
#	include <ppl.h>
#	ifdef Yield
#		undef Yield  // WinBase.h defines this :-/
#	endif
#else
#	include <semaphore>
#	include <thread>
#endif	// UNSYNC_USE_CONCRT
UNSYNC_THIRD_PARTY_INCLUDES_END

namespace unsync {

static constexpr uint32 UNSYNC_MAX_TOTAL_THREADS = 64;

extern uint32 GMaxThreads;

class FConcurrencyPolicyScope
{
public:
	UNSYNC_DISALLOW_COPY_ASSIGN(FConcurrencyPolicyScope)
	explicit FConcurrencyPolicyScope(uint32 MaxConcurrency);
	~FConcurrencyPolicyScope();
};

struct FThreadElectScope
{
	const bool			 bValue;	  // NOLINT
	const bool			 bCondition;  // NOLINT
	std::atomic<uint64>& Counter;
	FThreadElectScope(std::atomic<uint64>& InCounter, bool bInCondition)
	: bValue(bInCondition && (InCounter.fetch_add(1) == 0))
	, bCondition(bInCondition)
	, Counter(InCounter)
	{
	}
	~FThreadElectScope()
	{
		if (bCondition)
		{
			Counter.fetch_sub(1);
		}
	}

	operator bool() const { return bValue; }
};

void SchedulerSleep(uint32 Milliseconds);
void SchedulerYield();

#if UNSYNC_USE_CONCRT

// Cooperative semaphore implementation.
// Using this is necessary to avoid deadlocks on low-core machines.
// https://docs.microsoft.com/en-us/cpp/parallel/concrt/how-to-use-the-context-class-to-implement-a-cooperative-semaphore?view=msvc-160
class FSemaphore
{
public:
	UNSYNC_DISALLOW_COPY_ASSIGN(FSemaphore)

	explicit FSemaphore(uint32 MaxCount) : Counter(MaxCount) {}

	~FSemaphore() {}

	void Acquire()
	{
		if (--Counter < 0)
		{
			WaitingQueue.push(concurrency::Context::CurrentContext());
			concurrency::Context::Block();
		}
	}

	void Release()
	{
		if (++Counter <= 0)
		{
			concurrency::Context* Waiting = nullptr;
			while (!WaitingQueue.try_pop(Waiting))
			{
				concurrency::Context::YieldExecution();
			}
			Waiting->Unblock();
		}
	}

private:
	std::atomic<int64>									 Counter;
	concurrency::concurrent_queue<concurrency::Context*> WaitingQueue;
};

using FTaskGroup = concurrency::task_group;

template<typename IT, typename FT>
inline void
ParallelForEach(IT ItBegin, IT ItEnd, FT F)
{
	concurrency::parallel_for_each(ItBegin, ItEnd, F);
}

#else  // UNSYNC_USE_CONCRT

class FSemaphore
{
public:
	UNSYNC_DISALLOW_COPY_ASSIGN(FSemaphore)

	explicit FSemaphore(uint32 max_count) : native(max_count) {}

	~FSemaphore() {}

	void acquire() { native.acquire(); }

	void release() { native.release(); }

private:
	std::counting_semaphore<UNSYNC_MAX_TOTAL_THREADS> native;
};

// Single-threaded task group implementation
struct FTaskGroup
{
	template<typename F>
	void run(F f)
	{
		f();
	}
	void wait() {}
};

template<typename IT, typename FT>
inline void
ParallelForEach(IT ItBegin, IT ItEnd, FT F)
{
	for (; ItBegin != ItEnd; ++ItBegin)
	{
		F(*ItBegin);
	}
}

#endif	// UNSYNC_USE_CONCRT

}  // namespace unsync
