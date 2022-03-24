// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Templates/Function.h"
#include "Async/Async.h"
#include "TestHarness.h"

#include "TestCommon/CoreUtilities.h"

/** Helper methods used in the test cases. */
namespace AsyncTestUtils
{
	TFunction<int()> Task = [] {
		return 123;
	};

	bool bHasVoidTaskFinished = false;

	TFunction<void()> VoidTask = [] {
		bHasVoidTaskFinished = true;
	};
}


/** Test that task graph tasks return correctly. */
TEST_CASE("Core::Async::TaskGraph::Task Graph", "[Core][Async][Smoke]")
{
	InitTaskGraphAndDependencies();

	auto Future = Async(EAsyncExecution::TaskGraph, AsyncTestUtils::Task);
	int Result = Future.Get();

	TestEqual(TEXT("Task graph task must return expected value"), Result, 123);

	CleanupTaskGraphAndDependencies();
}


/** Test that threaded tasks return correctly. */
TEST_CASE("Core::Async::Thread::Thread", "[Core][Async][Smoke]")
{
	InitTaskGraphAndDependencies();

	auto Future = Async(EAsyncExecution::Thread, AsyncTestUtils::Task);
	int Result = Future.Get();

	REQUIRE(Result == 123);

	CleanupTaskGraphAndDependencies();
}


/** Test that threaded pool tasks return correctly. */
TEST_CASE("Core::Async::ThreadedPool::Threaded Pool", "[Core][Async][Smoke]")
{
	InitTaskGraphAndDependencies();

	auto Future = Async(EAsyncExecution::ThreadPool, AsyncTestUtils::Task);
	int Result = Future.Get();

	TestEqual(TEXT("Thread pool task must return expected value"), Result, 123);

	CleanupTaskGraphAndDependencies();
}


/** Test that void tasks run without errors or warnings. */
TEST_CASE("Core::Async::Task::Void Task", "[Core][Async][Smoke]")
{
	InitTaskGraphAndDependencies();

	// Reset test variable before running
	AsyncTestUtils::bHasVoidTaskFinished = false;
	auto Future = Async(EAsyncExecution::TaskGraph, AsyncTestUtils::VoidTask);
	Future.Get();

	// Check that the variable state was updated by task
	TestTrue(TEXT("Void tasks should run"), AsyncTestUtils::bHasVoidTaskFinished);

	CleanupTaskGraphAndDependencies();
}


/** Test that asynchronous tasks have their completion callback called. */
TEST_CASE("Core::Async::CompletionCallback::Completion Callback", "[Core][Async][Smoke]")
{
	InitTaskGraphAndDependencies();

	bool Completed = false;
	FEvent* CompletedEvent = FPlatformProcess::GetSynchEventFromPool(true);

	auto Future = Async(EAsyncExecution::TaskGraph, AsyncTestUtils::Task, [&] {
		Completed = true;
		CompletedEvent->Trigger();
	});

	int Result = Future.Get();

	// We need an additional synchronization point here since the future get above will return after
	// the task is done but before the completion callback has returned!

	bool CompletedEventTriggered = CompletedEvent->Wait(FTimespan(0 /* hours */, 0 /* minutes */, 5 /* seconds */));
	FPlatformProcess::ReturnSynchEventToPool(CompletedEvent);

	TestEqual(TEXT("Async Result"), Result, 123);
	TestTrue(TEXT("Completion callback to be called"), CompletedEventTriggered && Completed);

	CleanupTaskGraphAndDependencies();
}
