// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/PlatformMath.h"
#include "Misc/MonotonicTime.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace UE::HAL::Private
{

/**
 * A manual reset event that supports only one thread waiting and one thread signaling at a time.
 *
 * Only one waiting thread may call Reset() or the Wait() functions.
 * Only one signaling thread may call Signal() once until the event is reset.
 */
class FGenericPlatformManualResetEvent
{
public:
	FGenericPlatformManualResetEvent() = default;
	FGenericPlatformManualResetEvent(const FGenericPlatformManualResetEvent&) = delete;
	FGenericPlatformManualResetEvent& operator=(const FGenericPlatformManualResetEvent&) = delete;

	/**
	 * Resets the event to permit another Wait/Signal cycle.
	 *
	 * Must only be called by the waiting thread, and only when there is no possibility of waking
	 * occurring concurrently with the reset.
	 */
	void Reset()
	{
		bWait = true;
	}

	/**
	 * Waits for Signal() to be called.
	 *
	 * Signal() may be called prior to Wait(), and this will return immediately in that case.
	 */
	void Wait()
	{
		WaitUntil(FMonotonicTimePoint::Infinity());
	}

	/**
	 * Waits until the wait time for Signal() to be called.
	 *
	 * Signal() may be called prior to WaitUntil(), and this will return immediately in that case.
	 *
	 * @param WaitTime   Absolute time after which waiting is canceled and the thread wakes.
	 * @return True if Signal() was called before the wait time elapsed, otherwise false.
	 */
	bool WaitUntil(FMonotonicTimePoint WaitTime)
	{
		std::unique_lock SelfLock(Lock);
		if (WaitTime.IsInfinity())
		{
			Condition.wait(SelfLock, [this] { return !bWait; });
			return true;
		}
		if (FMonotonicTimeSpan WaitSpan = WaitTime - FMonotonicTimePoint::Now(); WaitSpan > FMonotonicTimeSpan::Zero())
		{
			const int64 WaitMs = FPlatformMath::CeilToInt64(WaitSpan.ToMilliseconds());
			return Condition.wait_for(SelfLock, std::chrono::milliseconds(WaitMs), [this] { return !bWait; });
		}
		return !bWait;
	}

	/**
	 * Signals the waiting thread.
	 *
	 * Signal() may be called prior to one of the wait functions, and the eventual wait call will
	 * return immediately when that occurs.
	 */
	void Signal()
	{
		{
			std::unique_lock SelfLock(Lock);
			bWait = false;
		}
		Condition.notify_one();
	}

private:
	std::mutex Lock;
	std::condition_variable Condition;
	std::atomic<bool> bWait = true;
};

} // UE::HAL::Private
