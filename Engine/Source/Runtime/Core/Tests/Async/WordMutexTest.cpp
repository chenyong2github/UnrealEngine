// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_LOW_LEVEL_TESTS

#include "Async/WordMutex.h"

#include "HAL/Thread.h"
#include "TestHarness.h"

namespace UE
{

TEST_CASE("Core::Async::WordMutex", "[Core][Async]")
{
	SECTION("FIFO")
	{
		constexpr static int32 TaskCount = 5;
		FThread Threads[TaskCount];
		std::atomic<int32> WaitCount = 0;
		int32 WakeCount = 0;
		int32 WakeStates[TaskCount];

		FWordMutex Mutex;
		Mutex.Lock();

		// Launch tasks that wait on the address of WaitCount.
		for (int32 Index = 0; Index < TaskCount; ++Index)
		{
			// Using FThread for now because UE::Tasks::Launch does not always wake a worker thread.
			Threads[Index] = FThread(TEXT("WordMutexTest"), [&Mutex, &WaitCount, &WakeCount, OutState = &WakeStates[Index]]
			{
				WaitCount.fetch_add(1);
				Mutex.Lock();
				*OutState = WakeCount++;
				Mutex.Unlock();
				WaitCount.fetch_sub(1);
			});

			// Spin until the task is about to lock.
			while (WaitCount != Index + 1)
			{
			}
		}

		// Unlock to allow each task to lock in sequence.
		Mutex.Unlock();

		// Spin until the tasks are complete.
		while (WaitCount != 0)
		{
		}

		// Verify that tasks woke in FIFO order.
		for (int32 Index = 0; Index < TaskCount; ++Index)
		{
			CHECK(WakeStates[Index] == Index);
		}

		// Wait for the threads to exit.
		for (FThread& Thread : Threads)
		{
			Thread.Join();
		}
	}
}

} // UE

#endif // WITH_LOW_LEVEL_TESTS
